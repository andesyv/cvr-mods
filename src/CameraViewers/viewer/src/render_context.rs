use std::ops::Neg;
use crate::create_external_semaphore;
use crate::external_image::ExternalImage;
use crate::platform::{
    IPCChannel, IPCSemaphore, MemoryExporter, SemaphoreReadyStatus, get_external_memory_type,
    get_external_semaphore_type,
};
use glam::{Mat4, Vec3};
use std::sync::Arc;
use uuid::Uuid;
use vulkano::buffer::{Buffer, BufferContents, BufferCreateInfo, BufferUsage, Subbuffer};
use vulkano::command_buffer::allocator::{CommandBufferAllocator, StandardCommandBufferAllocator};
use vulkano::command_buffer::{
    AutoCommandBufferBuilder, ClearColorImageInfo, CommandBufferUsage,
    PrimaryCommandBufferAbstract, RenderPassBeginInfo, SemaphoreSubmitInfo, SubmitInfo,
};
use vulkano::descriptor_set::allocator::StandardDescriptorSetAllocator;
use vulkano::descriptor_set::{DescriptorSet, WriteDescriptorSet};
use vulkano::device::physical::{PhysicalDevice, PhysicalDeviceType};
use vulkano::device::{
    Device, DeviceCreateInfo, DeviceExtensions, DeviceOwned, DeviceProperties, Queue,
    QueueCreateInfo, QueueFlags,
};
use vulkano::format::{ClearColorValue, Format};
use vulkano::image::sampler::{Filter, Sampler, SamplerCreateInfo};
use vulkano::image::view::ImageView;
use vulkano::image::{Image, ImageCreateInfo, ImageLayout, ImageType, ImageUsage};
use vulkano::instance::{Instance, InstanceCreateInfo};
use vulkano::memory::allocator::{AllocationCreateInfo, MemoryTypeFilter, StandardMemoryAllocator};
use vulkano::pipeline::graphics::GraphicsPipelineCreateInfo;
use vulkano::pipeline::graphics::color_blend::ColorBlendState;
use vulkano::pipeline::graphics::rasterization::{CullMode, RasterizationState};
use vulkano::pipeline::graphics::vertex_input::{Vertex, VertexDefinition};
use vulkano::pipeline::graphics::viewport::{Viewport, ViewportState};
use vulkano::pipeline::layout::PipelineDescriptorSetLayoutCreateInfo;
use vulkano::pipeline::{
    GraphicsPipeline, Pipeline, PipelineBindPoint, PipelineLayout, PipelineShaderStageCreateInfo,
};
use vulkano::render_pass::{Framebuffer, FramebufferCreateInfo, RenderPass, Subpass};
use vulkano::swapchain::{
    Surface, Swapchain, SwapchainCreateInfo, SwapchainPresentInfo, acquire_next_image,
};
use vulkano::sync::semaphore::Semaphore;
use vulkano::sync::{GpuFuture, PipelineStages};
use vulkano::{Validated, VulkanError, VulkanLibrary};
use winit::event_loop::ActiveEventLoop;
use winit::window::Window;

#[cfg(windows)]
pub const DEVICE_EXTENSIONS: DeviceExtensions = DeviceExtensions {
    khr_swapchain: true,
    khr_external_memory: true,
    khr_external_memory_win32: true,
    khr_external_semaphore: true,
    khr_external_semaphore_win32: true,
    khr_external_fence: true,
    khr_external_fence_win32: true,
    ..DeviceExtensions::empty()
};

#[cfg(unix)]
pub const DEVICE_EXTENSIONS: DeviceExtensions = DeviceExtensions {
    khr_swapchain: true,
    khr_external_memory: true,
    khr_external_memory_fd: true,
    khr_external_semaphore: true,
    khr_external_semaphore_fd: true,
    khr_external_fence: true,
    khr_external_fence_fd: true,
    // Eliminates the use for image layer transitions. However, it's not a part of Vulkano
    // (or probably any driver) yet:
    // https://www.khronos.org/blog/so-long-image-layouts-simplifying-vulkan-synchronisation
    // khr_unified_image_layouts: true,
    ..DeviceExtensions::empty()
};

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Vertex, BufferContents)]
pub struct MeshVertex {
    #[format(R32G32B32_SFLOAT)]
    position: [f32; 3],
    #[format(R32G32_SFLOAT)]
    tex_coord: [f32; 2],
}

fn create_instance(event_loop: &ActiveEventLoop) -> Arc<Instance> {
    let library = VulkanLibrary::new().unwrap();
    if library.api_version() < vulkano::Version::V1_1 {
        panic!("Vulkan 1.1 or higher is required");
    }

    const POSSIBLE_DEBUG_LAYERS: [Option<&str>; 3] = [
        Some("VK_LAYER_KHRONOS_validation"),         // The best one
        Some("VK_LAYER_LUNARG_standard_validation"), // The old one
        None,
    ];

    let mut required_extensions = Surface::required_extensions(event_loop).unwrap();
    // With ext_debug_utils we can specify a custom debug message handler. But I don't really have
    // a need for this as of right now.
    // required_extensions.ext_debug_utils = true;
    // Included in Vulkan 1.1, so not needed anymore.
    // required_extensions.khr_get_physical_device_properties2 = true;
    required_extensions.khr_external_memory_capabilities = true;
    required_extensions.khr_external_semaphore_capabilities = true;
    required_extensions.khr_external_fence_capabilities = true;

    let enabled_layers = 'a: {
        for wanted_layer in POSSIBLE_DEBUG_LAYERS.into_iter().flatten() {
            if library
                .layer_properties()
                .unwrap()
                .any(|available_layer| available_layer.name() == wanted_layer)
            {
                println!("Using validation layer: {}", wanted_layer);
                break 'a vec![wanted_layer.to_string()];
            }
        }
        vec![]
    };

    Instance::new(
        library.clone(),
        InstanceCreateInfo {
            application_name: Some(env!("CARGO_PKG_NAME").to_string()),
            application_version: env!("CARGO_PKG_VERSION").parse().unwrap(),
            enabled_extensions: required_extensions,
            enabled_layers,
            ..Default::default()
        },
    )
    .expect("Could not create Vulkan instance")
}

fn matches_target_driver_and_devices(
    target_driver: &Option<Uuid>,
    target_devices: &[Uuid],
    properties: &DeviceProperties,
) -> bool {
    if target_driver.is_none() && target_devices.is_empty() {
        return true;
    }

    match properties.driver_uuid.map(Uuid::from_bytes) {
        // driver cannot be determined by Vulkan and thus definitively does not match
        None => return false,
        Some(candidate_driver) if candidate_driver != target_driver.unwrap() => return false,
        _ => (),
    }

    match properties.device_uuid.map(Uuid::from_bytes) {
        // device cannot be determined by Vulkan and thus definitively does not match
        None => return false,
        Some(candidate_device) if !target_devices.contains(&candidate_device) => return false,
        _ => (),
    }

    true
}

fn find_physical_device_and_queue_family(
    instance: Arc<Instance>,
    surface: &Surface,
    target_driver: Option<Uuid>,
    target_devices: Vec<Uuid>,
) -> Option<(Arc<PhysicalDevice>, u32)> {
    instance
        .enumerate_physical_devices()
        .ok()?
        .filter_map(|d| {
            // Filter on driver and devices
            if !matches_target_driver_and_devices(&target_driver, &target_devices, d.properties()) {
                return None;
            }

            if d.supported_extensions().contains(&DEVICE_EXTENSIONS) {
                d.queue_family_properties()
                    .iter()
                    .enumerate()
                    .position(|(i, q)| {
                        q.queue_flags.contains(QueueFlags::GRAPHICS)
                            && d.surface_support(i as u32, surface).unwrap_or(false)
                    })
                    .map(|i| (d, i as u32))
            } else {
                None
            }
        })
        .min_by_key(|(d, _)| match d.properties().device_type {
            PhysicalDeviceType::DiscreteGpu => 0,
            PhysicalDeviceType::IntegratedGpu => 1,
            _ => 2,
        })
}

fn create_swapchain(
    device: &Arc<Device>,
    surface: &Arc<Surface>,
) -> Option<(Arc<Swapchain>, Vec<Arc<Image>>)> {
    let surface_capabilities = device
        .physical_device()
        .surface_capabilities(surface, Default::default())
        .unwrap();

    let image_format = device
        .physical_device()
        .surface_formats(surface, Default::default())
        .unwrap()
        .first()
        .map(|t| t.0)?;

    let image_extent = surface
        .object()
        .unwrap()
        .downcast_ref::<Window>()
        .unwrap()
        .inner_size()
        .into();

    Swapchain::new(
        device.clone(),
        surface.clone(),
        SwapchainCreateInfo {
            // min_image_count: surface_capabilities.min_image_count.max(2),
            min_image_count: surface_capabilities.min_image_count.max(3),
            image_format,
            image_extent,
            image_usage: ImageUsage::COLOR_ATTACHMENT,
            ..Default::default()
        },
    )
    .ok()
}

fn create_framebuffers(
    images: &[Arc<Image>],
    render_pass: &Arc<RenderPass>,
) -> (Vec<Arc<Framebuffer>>, Viewport) {
    let dimensions = images[0].extent();
    let dimensions = [dimensions[0] as f32, dimensions[1] as f32];

    let viewport = Viewport {
        offset: [0.0, 0.0],
        extent: dimensions,
        depth_range: 0.0..=1.0,
    };

    let framebuffers = images
        .iter()
        .map(|img| {
            let view = ImageView::new_default(img.clone())
                .map_err(Validated::unwrap)
                .unwrap();
            Framebuffer::new(
                render_pass.clone(),
                FramebufferCreateInfo {
                    attachments: vec![view],
                    ..Default::default()
                },
            )
            .map_err(Validated::unwrap)
            .unwrap()
        })
        .collect();

    (framebuffers, viewport)
}

fn create_descriptor_set(
    pipeline: &Arc<GraphicsPipeline>,
    device: &Arc<Device>,
    image_view: Arc<ImageView>,
) -> Arc<DescriptorSet> {
    let layout = &pipeline.layout().set_layouts()[0];
    let descriptor_set_allocator = Arc::new(StandardDescriptorSetAllocator::new(
        device.clone(),
        Default::default(),
    ));

    let sampler = Sampler::new(
        device.clone(),
        SamplerCreateInfo {
            mag_filter: Filter::Linear,
            min_filter: Filter::Linear,
            // address_mode: [SamplerAddressMode::Repeat; 3],
            ..Default::default()
        },
    )
    .unwrap();

    DescriptorSet::new(
        descriptor_set_allocator,
        layout.clone(),
        [WriteDescriptorSet::image_view_sampler(
            0, image_view, sampler,
        )],
        [],
    )
    .unwrap()
}

fn create_graphics_pipeline(
    device: Arc<Device>,
    viewport: Viewport,
    render_pass: Arc<RenderPass>,
) -> Arc<GraphicsPipeline> {
    let vs = vs::load(device.clone()).map_err(Validated::unwrap).unwrap();
    let fs = fs::load(device.clone()).map_err(Validated::unwrap).unwrap();

    let vs_entry_point = vs.entry_point("main").unwrap();
    let fs_entry_point = fs.entry_point("main").unwrap();

    let vertex_input_state = MeshVertex::per_vertex()
        .definition(&vs_entry_point)
        .unwrap();

    let stages = [vs_entry_point, fs_entry_point]
        .into_iter()
        .map(PipelineShaderStageCreateInfo::new)
        .collect();

    let layout = PipelineLayout::new(
        device.clone(),
        PipelineDescriptorSetLayoutCreateInfo::from_stages(&stages)
            .into_pipeline_layout_create_info(device.clone())
            .unwrap(),
    )
    .map_err(Validated::unwrap)
    .unwrap();

    let subpass = Subpass::from(render_pass, 0).unwrap();

    GraphicsPipeline::new(
        device,
        None,
        GraphicsPipelineCreateInfo {
            stages,
            vertex_input_state: Some(vertex_input_state),
            input_assembly_state: Some(Default::default()),
            viewport_state: Some(ViewportState {
                viewports: [viewport].into_iter().collect(),
                ..Default::default()
            }),
            rasterization_state: Some(RasterizationState {
                cull_mode: CullMode::Back,
                ..Default::default()
            }),
            // depth_stencil_state: Some(DepthStencilState {
            //     depth: Some(DepthState::simple()),
            //     ..Default::default()
            // }),
            multisample_state: Some(Default::default()),
            color_blend_state: Some(ColorBlendState::with_attachment_states(
                subpass.num_color_attachments(),
                Default::default(),
            )),
            subpass: Some(subpass.into()),
            ..GraphicsPipelineCreateInfo::layout(layout)
        },
    )
    .unwrap()
}

fn create_perspective(width: u32, height: u32) -> Mat4 {
    let mut m = Mat4::perspective_rh(45f32.to_radians(), width as f32 / height as f32, 0.1, 100.0);
    // glam creates an OpenGL / Direct3D perspective matrix. However, in Vulkan, the vertical
    // axis in the clip space is flipped (going from the top left to the bottom right instead of
    // the more familiar bottom left to top right). Therefore, to make this perspective matrix
    // correct for the Vulkan coordinate system, we flip the second axis component scale,
    // flipping the y-axis. Note that this in turn will also flip the z-axis.
    m.y_axis.y = -m.y_axis.y;
    m
}

mod vs {
    vulkano_shaders::shader! {
        ty: "vertex",
        path: "src/shaders/default.vert"
    }
}

mod fs {
    vulkano_shaders::shader! {
        ty: "fragment",
        path: "src/shaders/default.frag"
    }
}

struct ExternalCommunication {
    begin_sem: Arc<Semaphore>,
    end_sem: Arc<Semaphore>,
    host_process_semaphore: IPCSemaphore,
}

#[derive(Default)]
pub struct RenderContextCreationInfo {
    pub driver: Option<Uuid>,
    pub devices: Vec<Uuid>,
    pub owner_channel: Option<IPCChannel>,
}

pub struct RenderContext {
    pipeline: Arc<GraphicsPipeline>,
    command_buffer_allocator: Arc<dyn CommandBufferAllocator>,
    queue: Arc<Queue>,
    // queue_family_index: u32,
    vertex_buffer: Subbuffer<[MeshVertex]>,
    framebuffers: Vec<Arc<Framebuffer>>,
    swapchain: Arc<Swapchain>,
    swapchain_fences: Vec<Option<Box<dyn GpuFuture>>>,
    external: Option<ExternalCommunication>,
    image_descriptor_set: Arc<DescriptorSet>,
    last_submitted_swapchain_image_index: usize,
    perspective: Mat4,
}

pub enum DrawResult {
    Ok,
    /// Swapchain is outdated and needs to be recreated. The frame may or may not have been submitted.
    SwapchainOutdated,
    /// The host is currently holding us hostage, and we're not allowed to draw yet
    WaitingForHost,
    /// We lost our connection to the host and should exit
    HostDisconnected,
}

impl RenderContext {
    pub fn new(
        event_loop: &ActiveEventLoop,
        window: Arc<Window>,
        dimensions: [u32; 2],
        creation_info: RenderContextCreationInfo,
    ) -> Self {
        let instance = create_instance(event_loop);
        let api_version = instance.api_version();
        // let _debug_messenger = unsafe { window::create_debug_messenger(&instance) };

        let surface = Surface::from_window(instance.clone(), window).unwrap();
        let (physical_device, queue_family_index) = find_physical_device_and_queue_family(
            instance,
            &surface,
            creation_info.driver,
            creation_info.devices,
        )
        .expect("No suitable GPU or driver found");

        if cfg!(debug_assertions) {
            println!(
                "Using {} and vulkan {:?}",
                physical_device.properties().device_name,
                api_version
            );
        }

        let (device, mut queue) = Device::new(
            physical_device.clone(),
            DeviceCreateInfo {
                enabled_extensions: DEVICE_EXTENSIONS,
                queue_create_infos: vec![QueueCreateInfo {
                    queue_family_index,
                    ..Default::default()
                }],
                ..Default::default()
            },
        )
        .map_err(Validated::unwrap)
        .unwrap();
        let queue = queue.next().unwrap();

        let (swapchain, swapchain_images) = create_swapchain(&device, &surface).unwrap();

        let memory_allocator = Arc::new(StandardMemoryAllocator::new_default(device.clone()));

        let render_pass = vulkano::single_pass_renderpass!(
            device.clone(),
            attachments: {
                color: {
                    format: swapchain.image_format(),
                    samples: 1,
                    load_op: Clear,
                    store_op: Store,
                }
            },
            pass: {
                color: [color],
                depth_stencil: {}
            }
        )
        .map_err(Validated::unwrap)
        .unwrap();

        // Cube vertex data
        pub const CUBE_VERTICES: [MeshVertex; 36] = [
            // Front
            MeshVertex {
                position: [-0.5, -0.5, 0.5],
                tex_coord: [0.0, 0.0],
            },
            MeshVertex {
                position: [0.5, -0.5, 0.5],
                tex_coord: [1.0, 0.0],
            },
            MeshVertex {
                position: [0.5, 0.5, 0.5],
                tex_coord: [1.0, 1.0],
            },
            MeshVertex {
                position: [0.5, 0.5, 0.5],
                tex_coord: [1.0, 1.0],
            },
            MeshVertex {
                position: [-0.5, 0.5, 0.5],
                tex_coord: [0.0, 1.0],
            },
            MeshVertex {
                position: [-0.5, -0.5, 0.5],
                tex_coord: [0.0, 0.0],
            },
            // Right
            MeshVertex {
                position: [0.5, -0.5, 0.5],
                tex_coord: [0.0, 0.0],
            },
            MeshVertex {
                position: [0.5, -0.5, -0.5],
                tex_coord: [1.0, 0.0],
            },
            MeshVertex {
                position: [0.5, 0.5, -0.5],
                tex_coord: [1.0, 1.0],
            },
            MeshVertex {
                position: [0.5, 0.5, -0.5],
                tex_coord: [1.0, 1.0],
            },
            MeshVertex {
                position: [0.5, 0.5, 0.5],
                tex_coord: [0.0, 1.0],
            },
            MeshVertex {
                position: [0.5, -0.5, 0.5],
                tex_coord: [0.0, 0.0],
            },
            // Back
            MeshVertex {
                position: [0.5, -0.5, -0.5],
                tex_coord: [0.0, 0.0],
            },
            MeshVertex {
                position: [-0.5, -0.5, -0.5],
                tex_coord: [1.0, 0.0],
            },
            MeshVertex {
                position: [-0.5, 0.5, -0.5],
                tex_coord: [1.0, 1.0],
            },
            MeshVertex {
                position: [-0.5, 0.5, -0.5],
                tex_coord: [1.0, 1.0],
            },
            MeshVertex {
                position: [0.5, 0.5, -0.5],
                tex_coord: [0.0, 1.0],
            },
            MeshVertex {
                position: [0.5, -0.5, -0.5],
                tex_coord: [0.0, 0.0],
            },
            // Left
            MeshVertex {
                position: [-0.5, -0.5, -0.5],
                tex_coord: [0.0, 0.0],
            },
            MeshVertex {
                position: [-0.5, -0.5, 0.5],
                tex_coord: [1.0, 0.0],
            },
            MeshVertex {
                position: [-0.5, 0.5, 0.5],
                tex_coord: [1.0, 1.0],
            },
            MeshVertex {
                position: [-0.5, 0.5, 0.5],
                tex_coord: [1.0, 1.0],
            },
            MeshVertex {
                position: [-0.5, 0.5, -0.5],
                tex_coord: [0.0, 1.0],
            },
            MeshVertex {
                position: [-0.5, -0.5, -0.5],
                tex_coord: [0.0, 0.0],
            },
            // Top
            MeshVertex {
                position: [-0.5, 0.5, 0.5],
                tex_coord: [0.0, 0.0],
            },
            MeshVertex {
                position: [0.5, 0.5, 0.5],
                tex_coord: [1.0, 0.0],
            },
            MeshVertex {
                position: [0.5, 0.5, -0.5],
                tex_coord: [1.0, 1.0],
            },
            MeshVertex {
                position: [0.5, 0.5, -0.5],
                tex_coord: [1.0, 1.0],
            },
            MeshVertex {
                position: [-0.5, 0.5, -0.5],
                tex_coord: [0.0, 1.0],
            },
            MeshVertex {
                position: [-0.5, 0.5, 0.5],
                tex_coord: [0.0, 0.0],
            },
            // Bottom
            MeshVertex {
                position: [0.5, -0.5, 0.5],
                tex_coord: [0.0, 0.0],
            },
            MeshVertex {
                position: [-0.5, -0.5, 0.5],
                tex_coord: [1.0, 0.0],
            },
            MeshVertex {
                position: [-0.5, -0.5, -0.5],
                tex_coord: [1.0, 1.0],
            },
            MeshVertex {
                position: [-0.5, -0.5, -0.5],
                tex_coord: [1.0, 1.0],
            },
            MeshVertex {
                position: [0.5, -0.5, -0.5],
                tex_coord: [0.0, 1.0],
            },
            MeshVertex {
                position: [0.5, -0.5, 0.5],
                tex_coord: [0.0, 0.0],
            },
        ];

        let vertex_buffer = Buffer::from_iter(
            memory_allocator.clone(),
            BufferCreateInfo {
                usage: BufferUsage::VERTEX_BUFFER,
                ..Default::default()
            },
            AllocationCreateInfo {
                memory_type_filter: MemoryTypeFilter::PREFER_DEVICE
                    | MemoryTypeFilter::HOST_SEQUENTIAL_WRITE,
                ..Default::default()
            },
            CUBE_VERTICES,
        )
        .unwrap();

        let (framebuffers, viewport) = create_framebuffers(&swapchain_images, &render_pass);
        let pipeline = create_graphics_pipeline(device.clone(), viewport, render_pass);

        let command_buffer_allocator = Arc::new(StandardCommandBufferAllocator::new(
            device.clone(),
            Default::default(),
        ));

        // Logic taken from https://github.com/vulkano-rs/vulkano/blob/master/examples/gl-interop/main.rs
        // (which is based on https://github.com/KhronosGroup/Vulkan-Samples/blob/main/samples/extensions/open_gl_interop/open_gl_interop.cpp)
        let (external, image_descriptor_set) = if let Some(channel) = creation_info.owner_channel {
            let semaphore_handle_type = get_external_semaphore_type(&physical_device).unwrap();
            let memory_handle_type = get_external_memory_type(
                &physical_device,
                BufferUsage::TRANSFER_SRC
                    | BufferUsage::TRANSFER_DST
                    | BufferUsage::STORAGE_TEXEL_BUFFER,
            )
            .unwrap();

            let begin_sem =
                create_external_semaphore(device.clone(), semaphore_handle_type.into()).unwrap();
            let end_sem =
                create_external_semaphore(device.clone(), semaphore_handle_type.into()).unwrap();

            let shared_image = ExternalImage::new(
                device.clone(),
                &memory_allocator,
                dimensions,
                memory_handle_type,
            )
            .unwrap();

            let mut memory_exporter = MemoryExporter::from_channel(channel);
            // The host (OGL) takes ownership of the image as soon as we're done with using it,
            // signalled by end_sem. Therefore, VK end = OGL begin and OGL end = VK begin
            memory_exporter.export_semaphore_to_owner_process(
                "host_begin_sem",
                &end_sem,
                semaphore_handle_type,
            );
            memory_exporter.export_semaphore_to_owner_process(
                "host_end_sem",
                &begin_sem,
                semaphore_handle_type,
            );
            memory_exporter.export_memory_to_owner_process(
                "shared_image",
                &shared_image,
                memory_handle_type,
            );
            let mut host_process_semaphore = memory_exporter.flush_and_transform_to_semaphore();
            let image_view = ImageView::new_default(shared_image.into()).unwrap();
            let shared_image_descriptor_set = create_descriptor_set(&pipeline, &device, image_view);

            // The semaphores are initialized in an unsignaled state. We want the host to start
            // rendering first, so we need to signal the semaphore so the host can start.
            queue
                .with(|mut q| unsafe {
                    q.submit_unchecked(
                        &[SubmitInfo {
                            signal_semaphores: vec![SemaphoreSubmitInfo::new(end_sem.clone())],
                            ..Default::default()
                        }],
                        None,
                    )
                })
                .unwrap();

            host_process_semaphore.signal();

            (
                Some(ExternalCommunication {
                    begin_sem,
                    end_sem,
                    host_process_semaphore,
                }),
                shared_image_descriptor_set,
            )
        } else {
            println!("Running without an attached host process (a.k.a. debug mode)");

            // Our shader expects a descriptor set, so create a dummy image:
            let image = Image::new(
                memory_allocator,
                ImageCreateInfo {
                    image_type: ImageType::Dim2d,
                    format: Format::R8G8B8A8_SRGB,
                    extent: [64, 64, 1],
                    usage: ImageUsage::SAMPLED | ImageUsage::TRANSFER_DST,
                    ..Default::default()
                },
                Default::default(),
            )
            .unwrap();

            // Issue an immediate command to paint the texture with a colour
            let mut builder = AutoCommandBufferBuilder::primary(
                command_buffer_allocator.clone(),
                queue.queue_family_index(),
                CommandBufferUsage::OneTimeSubmit,
            )
            .map_err(Validated::unwrap)
            .unwrap();

            builder
                .clear_color_image(ClearColorImageInfo {
                    clear_value: ClearColorValue::Float([1.0, 0.0, 1.0, 1.0]),
                    image_layout: ImageLayout::General,
                    ..ClearColorImageInfo::image(image.clone())
                })
                .unwrap();
            let command_buffer = builder.build().unwrap();
            let future = command_buffer.execute(queue.clone()).unwrap();
            future.flush().unwrap();

            let image_view = ImageView::new_default(image).unwrap();
            let image_descriptor_set = create_descriptor_set(&pipeline, &device, image_view);

            (None, image_descriptor_set)
        };

        Self {
            pipeline,
            command_buffer_allocator,
            queue,
            vertex_buffer,
            framebuffers,
            swapchain,
            swapchain_fences: Vec::new(),
            external,
            image_descriptor_set,
            last_submitted_swapchain_image_index: 0,
            perspective: create_perspective(dimensions[0], dimensions[1]),
        }
    }

    pub fn draw(&mut self, time: f32) -> DrawResult {
        if let Some(ExternalCommunication {
            host_process_semaphore,
            begin_sem,
            ..
        }) = self.external.as_mut()
        {
            // As both the host and the client process make use of the same graphics device,
            // the graphics queue will be intermingled with commands from both processes.
            // In order for the semaphore order logic to be correct, we therefore need to ensure the
            // processes are sequentially drawing frames one after another. That way both processes
            // submit graphics commands which will end up in the expected order on the graphics
            // device.
            match host_process_semaphore.ready_status() {
                SemaphoreReadyStatus::NotReady => return DrawResult::WaitingForHost,
                SemaphoreReadyStatus::ConnectionLost => return DrawResult::HostDisconnected,
                SemaphoreReadyStatus::Ready => (),
            }

            self.queue
                .with(|mut q| unsafe {
                    q.submit_unchecked(
                        &[SubmitInfo {
                            wait_semaphores: vec![SemaphoreSubmitInfo {
                                stages: PipelineStages::FRAGMENT_SHADER,
                                ..SemaphoreSubmitInfo::new(begin_sem.clone())
                            }],
                            ..Default::default()
                        }],
                        None,
                    )
                })
                .unwrap();
        }

        let mut builder = AutoCommandBufferBuilder::primary(
            self.command_buffer_allocator.clone(),
            self.queue.queue_family_index(),
            CommandBufferUsage::OneTimeSubmit,
        )
        .map_err(Validated::unwrap)
        .unwrap();

        let (image_index, mut swapchain_outdated, acquire_future) =
            acquire_next_image(self.swapchain.clone(), None).unwrap();

        builder
            .begin_render_pass(
                RenderPassBeginInfo {
                    clear_values: vec![Some([0.0, 0.15, 0.2, 1.0].into())],
                    ..RenderPassBeginInfo::framebuffer(
                        self.framebuffers[image_index as usize].clone(),
                    )
                },
                Default::default(),
            )
            .unwrap();

        let pipeline_layout = self.pipeline.layout().clone();
        builder
            .bind_pipeline_graphics(self.pipeline.clone())
            .unwrap()
            .bind_vertex_buffers(0, self.vertex_buffer.clone())
            .unwrap()
            .bind_descriptor_sets(
                PipelineBindPoint::Graphics,
                self.pipeline.layout().clone(),
                0,
                self.image_descriptor_set.clone(),
            )
            .unwrap();

        let view = Mat4::look_at_rh(
            Vec3::new(time.neg().sin() * 5.0, 3.0, time.neg().cos() * 5.0),
            Vec3::ZERO,
            Vec3::Y,
        );
        let mvp = self.perspective * view;
        let push_constants = vs::FrameData {
            time: time.into(),
            mvp: mvp.to_cols_array_2d(),
        };

        builder
            .push_constants(pipeline_layout.clone(), 0, push_constants)
            .unwrap();

        unsafe {
            builder.draw(36, 1, 0, 0).unwrap();
        }

        builder.end_render_pass(Default::default()).unwrap();

        let command_buffer = builder.build().map_err(Validated::unwrap).unwrap();

        // In case we cought up to our maximum frames-in-flight, finish waiting for the current
        // frame to finish from last time
        self.swapchain_fences
            .get_mut(image_index as usize)
            .map(|inner| inner.take());

        let current_future = if let Some(last_frame_future) = self
            .swapchain_fences
            .get_mut(self.last_submitted_swapchain_image_index)
            .and_then(Option::take)
        {
            last_frame_future.join(acquire_future).boxed()
        } else {
            acquire_future.boxed()
        };

        match current_future
            .then_execute(self.queue.clone(), command_buffer)
            .unwrap()
            .then_swapchain_present(
                self.queue.clone(),
                SwapchainPresentInfo::swapchain_image_index(self.swapchain.clone(), image_index),
            )
            .then_signal_fence_and_flush()
            .map_err(Validated::unwrap)
        {
            Ok(current_future) => {
                if self.swapchain_fences.len() < image_index as usize + 1 {
                    self.swapchain_fences.push(Some(current_future.boxed()));
                } else {
                    self.swapchain_fences[image_index as usize] = Some(current_future.boxed());
                }

                self.last_submitted_swapchain_image_index = image_index as usize;
            }
            Err(VulkanError::OutOfDate) => swapchain_outdated = true,
            Err(e) => panic!("Failed to submit new frame: {:?}", e),
        }

        if let Some(ExternalCommunication {
            host_process_semaphore,
            end_sem,
            ..
        }) = self.external.as_mut()
        {
            self.queue
                .with(|mut q| unsafe {
                    q.submit_unchecked(
                        &[SubmitInfo {
                            signal_semaphores: vec![SemaphoreSubmitInfo::new(end_sem.clone())],
                            ..Default::default()
                        }],
                        None,
                    )
                })
                .unwrap();

            if !host_process_semaphore.signal() {
                return DrawResult::HostDisconnected;
            }
        }

        if swapchain_outdated {
            DrawResult::SwapchainOutdated
        } else {
            DrawResult::Ok
        }
    }

    pub fn recreate_swapchain(&mut self, width: u32, height: u32) {
        // First, we need to synchronize the CPU and the GPU by waiting for the frames-in-flight
        self.swapchain_fences.clear();

        // First, fetch the device from the current swapchain
        let device = self.swapchain.device().clone();

        // Use the convenient vulkano::Swapchain::recreate function for recreating the swapchain
        let (swapchain, swapchain_images) = self
            .swapchain
            .recreate(SwapchainCreateInfo {
                image_extent: [width, height],
                ..self.swapchain.create_info()
            })
            .expect("Failed to recreate swapchain");
        if swapchain_images.is_empty() {
            panic!("Swapchain contains no images");
        }

        self.swapchain = swapchain;

        // Fetch the render pass from the current framebuffer (and keep it alive)
        let render_pass = self.framebuffers.first().unwrap().render_pass().clone();

        let (framebuffers, viewport) = create_framebuffers(&swapchain_images[..], &render_pass);

        self.framebuffers = framebuffers;

        // Finally, recreate the graphics pipeline as well as that one makes use of the viewport
        self.pipeline = create_graphics_pipeline(device, viewport, render_pass);
        self.perspective = create_perspective(width, height);
    }
}
