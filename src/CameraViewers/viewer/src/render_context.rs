use crate::create_external_semaphore;
use crate::external_image::ExternalImage;
use crate::platform::{get_external_memory_type, get_external_semaphore_type, MemoryExporter};
use cgmath::{Deg, Matrix4, PerspectiveFov, Point3, Vector3};
use std::sync::Arc;
use std::time::Duration;
use vulkano::buffer::{Buffer, BufferContents, BufferCreateInfo, BufferUsage, Subbuffer};
use vulkano::command_buffer::allocator::{CommandBufferAllocator, StandardCommandBufferAllocator};
use vulkano::command_buffer::{AutoCommandBufferBuilder, CommandBufferUsage, RenderPassBeginInfo};
use vulkano::device::physical::{PhysicalDevice, PhysicalDeviceType};
use vulkano::device::{
    Device, DeviceCreateInfo, DeviceExtensions, Queue, QueueCreateInfo, QueueFlags,
};
use vulkano::image::view::ImageView;
use vulkano::image::{Image, ImageUsage};
use vulkano::instance::{Instance, InstanceCreateInfo};
use vulkano::memory::allocator::{AllocationCreateInfo, MemoryTypeFilter, StandardMemoryAllocator};
use vulkano::pipeline::graphics::GraphicsPipelineCreateInfo;
use vulkano::pipeline::graphics::color_blend::ColorBlendState;
use vulkano::pipeline::graphics::rasterization::{CullMode, RasterizationState};
use vulkano::pipeline::graphics::vertex_input::{Vertex, VertexDefinition};
use vulkano::pipeline::graphics::viewport::{Viewport, ViewportState};
use vulkano::pipeline::layout::PipelineDescriptorSetLayoutCreateInfo;
use vulkano::pipeline::{
    GraphicsPipeline, Pipeline, PipelineLayout, PipelineShaderStageCreateInfo,
};
use vulkano::render_pass::{Framebuffer, FramebufferCreateInfo, RenderPass, Subpass};
use vulkano::swapchain::{Surface, Swapchain, SwapchainCreateInfo, acquire_next_image};
use vulkano::sync::GpuFuture;
use vulkano::{Validated, VulkanLibrary};
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
    ..DeviceExtensions::empty()
};

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Vertex, BufferContents)]
pub struct MyVertex {
    #[format(R32G32B32_SFLOAT)]
    position: [f32; 3],
    #[format(R32G32_SFLOAT)]
    tex_coord: [f32; 2],
}

impl MyVertex {
    pub const fn new(position: [f32; 3], tex_coord: [f32; 2]) -> Self {
        MyVertex {
            position,
            tex_coord,
        }
    }
}

pub fn create_instance(event_loop: &ActiveEventLoop) -> Arc<Instance> {
    let library = VulkanLibrary::new().unwrap();
    const POSSIBLE_DEBUG_LAYERS: [Option<&str>; 3] = [
        Some("VK_LAYER_KHRONOS_validation"),         // The best one
        Some("VK_LAYER_LUNARG_standard_validation"), // The old one
        None,
    ];

    let mut required_extensions = Surface::required_extensions(event_loop).unwrap();
    required_extensions.ext_debug_utils = true;
    required_extensions.khr_get_physical_device_properties2 = true;
    required_extensions.khr_external_memory_capabilities = true;
    required_extensions.khr_external_semaphore_capabilities = true;
    required_extensions.khr_external_fence_capabilities = true;

    let enabled_layers = 'a: {
        for debug_layer in POSSIBLE_DEBUG_LAYERS {
            if let Some(wanted_layer) = debug_layer {
                if library
                    .layer_properties()
                    .unwrap()
                    .any(|available_layer| available_layer.name() == wanted_layer)
                {
                    println!("Using validation layer: {}", wanted_layer);
                    break 'a vec![wanted_layer.to_string()];
                }
            }
        }
        vec![]
    };

    Instance::new(
        library.clone(),
        InstanceCreateInfo {
            application_name: Some("Viewer".to_string()),
            application_version: env!("CARGO_PKG_VERSION").parse().unwrap(),
            enabled_extensions: required_extensions,
            enabled_layers: enabled_layers,
            ..Default::default()
        },
    )
    .map_err(Validated::unwrap)
    .expect("Could not create Vulkan instance")
}

// #[cfg(debug_assertions)]
// fn debug_message_callback(
//     severity: DebugUtilsMessageSeverity,
//     message_type: DebugUtilsMessageType,
//     callback_data: DebugUtilsMessengerCallbackData<'_>,
// ) {
//     match severity {
//         DebugUtilsMessageSeverity::INFO | DebugUtilsMessageSeverity::VERBOSE => {
//             println!("Vulkan Info: {}", callback_data.message)
//         }
//         _ => println!(
//             "Vulkan Error (Type: {:?}): {}",
//             message_type, callback_data.message
//         ),
//     };
// }

// #[cfg(not(debug_assertions))]
// fn debug_message_callback(message: &Message) {
//     match message.severity {
//         DebugUtilsMessageSeverity {
//             information: false,
//             verbose: false,
//             ..
//         } => println!("Error (Type: {:?}): {}", message.ty, message.description),
//         _ => (),
//     };
// }

// pub unsafe fn create_debug_messenger(instance: &Arc<Instance>) -> DebugUtilsMessenger {
//     DebugUtilsMessenger::new(
//         instance.clone(),
//         DebugUtilsMessengerCreateInfo {
//             message_severity: DebugUtilsMessageSeverity {
//                 error: true,
//                 warning: true,
//                 information: true,
//                 verbose: true,
//                 ..Default::default()
//             },
//             message_type: DebugUtilsMessageType {
//                 general: true,
//                 validation: true,
//                 performance: true,
//                 ..Default::default()
//             },
//             ..DebugUtilsMessengerCreateInfo::user_callback(Arc::new(debug_message_callback))
//         },
//     )
//     .unwrap()
// }

pub fn find_physical_device_and_queue_family(
    instance: Arc<Instance>,
    surface: &Surface,
) -> Option<(Arc<PhysicalDevice>, u32)> {
    instance
        .enumerate_physical_devices()
        .ok()?
        .filter_map(|d| {
            if d.supported_extensions().contains(&DEVICE_EXTENSIONS) {
                d.queue_family_properties()
                    .iter()
                    .enumerate()
                    .position(|(i, q)| {
                        q.queue_flags.contains(QueueFlags::GRAPHICS)
                            && d.surface_support(i as u32, &surface).unwrap_or(false)
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

pub fn create_swapchain(
    device: &Arc<Device>,
    surface: &Arc<Surface>,
) -> Option<(Arc<Swapchain>, Vec<Arc<Image>>)> {
    let mut surface_capabilities = device
        .physical_device()
        .surface_capabilities(surface, Default::default())
        .unwrap();
    surface_capabilities.min_image_count = surface_capabilities.min_image_count.max(3);

    let image_format = device
        .physical_device()
        .surface_formats(&surface, Default::default())
        .unwrap()
        .first()
        .and_then(|t| Some(t.0))?;

    Swapchain::new(
        device.clone(),
        surface.clone(),
        SwapchainCreateInfo {
            min_image_count: surface_capabilities.min_image_count,
            image_format,
            image_extent: surface
                .object()
                .unwrap()
                .downcast_ref::<Window>()
                .unwrap()
                .inner_size()
                .into(),
            image_usage: ImageUsage::COLOR_ATTACHMENT,
            ..Default::default()
        },
    )
    .ok()
}

pub fn create_framebuffers(
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

    // let perspective = Matrix4::from(PerspectiveFov {
    //     fovy: Deg(45.0).into(),
    //     aspect: dimensions[0] / dimensions[1],
    //     near: 0.1,
    //     far: 100.0,
    // });

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

mod vs {
    vulkano_shaders::shader! {
        ty: "vertex",
        src: "
            #version 460
            layout(location = 0) in vec3 position;
            layout(location = 1) in vec2 tex_coord;
            layout(push_constant) uniform FrameData {
                float time;
                mat4 mvp;
            } frame_data;
            layout(location = 0) out vec2 uv;
            void main() {
                uv = tex_coord;
                gl_Position = frame_data.mvp * vec4(position, 1.0);
            }
            "
    }
}

mod fs {
    vulkano_shaders::shader! {
        ty: "fragment",
        src: "
            #version 460

            layout(location = 0) in vec2 uv;
            // layout(set = 0, binding = 0) uniform sampler2D tex;
            layout(location = 0) out vec4 frag_colour;

            void main() {
                // frag_colour = vec4(texture(tex, uv).rgb, 1.0);
                frag_colour = vec4(uv, 0.0, 1.0);
            }
            "
    }
}

pub struct RenderContext {
    pipeline: Arc<GraphicsPipeline>,
    command_buffer_allocator: Arc<dyn CommandBufferAllocator>,
    queue: Arc<Queue>,
    // queue_family_index: u32,
    vertex_buffer: Subbuffer<[MyVertex]>,
    framebuffers: Vec<Arc<Framebuffer>>,
    swapchain: Arc<Swapchain>,
    swapchain_fences: Vec<Option<Box<dyn GpuFuture>>>,
    // Has to be kept alive but will never be used
    pub(crate) memory_exporter: MemoryExporter,
}

impl RenderContext {
    pub fn new(event_loop: &ActiveEventLoop, window: Arc<Window>, dimensions: [u32; 2]) -> Self {
        let instance = create_instance(event_loop);
        let api_version = instance.api_version();
        // let _debug_messenger = unsafe { window::create_debug_messenger(&instance) };

        let surface = Surface::from_window(instance.clone(), window).unwrap();
        let (physical_device, queue_family_index) =
            find_physical_device_and_queue_family(instance, &surface).unwrap();

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

        // Logic taken from https://github.com/vulkano-rs/vulkano/blob/master/examples/gl-interop/main.rs
        // (which is based on https://github.com/KhronosGroup/Vulkan-Samples/blob/main/samples/extensions/open_gl_interop/open_gl_interop.cpp)

        let semaphore_handle_type = get_external_semaphore_type(&physical_device).unwrap();
        let memory_handle_type = get_external_memory_type(
            &physical_device,
            BufferUsage::TRANSFER_SRC
                | BufferUsage::TRANSFER_DST
                | BufferUsage::STORAGE_TEXEL_BUFFER,
        )
        .unwrap();

        let vk_begin_sem =
            create_external_semaphore(device.clone(), semaphore_handle_type.into()).unwrap();
        let vk_end_sem =
            create_external_semaphore(device.clone(), semaphore_handle_type.into()).unwrap();

        let mut memory_exporter = MemoryExporter::default();
        memory_exporter.export_semaphore_to_owner_process("OGL_begin", &vk_end_sem, semaphore_handle_type);
        memory_exporter.export_semaphore_to_owner_process("OGL_end", &vk_begin_sem, semaphore_handle_type);

        // TODO: Make a version that works on Windows (POSIX file descriptor handles only works on Unix)
        let image = ExternalImage::new(
            device.clone(),
            &memory_allocator,
            dimensions,
            memory_handle_type,
        )
        .unwrap();

        memory_exporter.export_memory_to_owner_process("OGL_buffer", &image, memory_handle_type);

        // let image_view = image.try_into().unwrap();

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
        const VERTICES: [MyVertex; 36] = [
            // Top
            MyVertex::new([1.0, 1.0, -1.0], [1.0, 1.0]),
            MyVertex::new([-1.0, -1.0, -1.0], [0.0, 0.0]),
            MyVertex::new([-1.0, 1.0, -1.0], [0.0, 1.0]),
            MyVertex::new([1.0, 1.0, -1.0], [1.0, 1.0]),
            MyVertex::new([1.0, -1.0, -1.0], [1.0, 0.0]),
            MyVertex::new([-1.0, -1.0, -1.0], [0.0, 0.0]),
            // Side
            MyVertex::new([-1.0, -1.0, -1.0], [1.0, 1.0]),
            MyVertex::new([-1.0, -1.0, 1.0], [1.0, 0.0]),
            MyVertex::new([-1.0, 1.0, 1.0], [0.0, 0.0]),
            MyVertex::new([-1.0, -1.0, -1.0], [1.0, 1.0]),
            MyVertex::new([-1.0, 1.0, 1.0], [0.0, 0.0]),
            MyVertex::new([-1.0, 1.0, -1.0], [0.0, 1.0]),
            // Side
            MyVertex::new([1.0, -1.0, 1.0], [1.0, 0.0]),
            MyVertex::new([-1.0, -1.0, -1.0], [0.0, 1.0]),
            MyVertex::new([1.0, -1.0, -1.0], [1.0, 1.0]),
            MyVertex::new([1.0, -1.0, 1.0], [1.0, 0.0]),
            MyVertex::new([-1.0, -1.0, 1.0], [0.0, 0.0]),
            MyVertex::new([-1.0, -1.0, -1.0], [0.0, 1.0]),
            // Side
            MyVertex::new([1.0, 1.0, 1.0], [1.0, 0.0]),
            MyVertex::new([1.0, -1.0, -1.0], [0.0, 1.0]),
            MyVertex::new([1.0, 1.0, -1.0], [1.0, 1.0]),
            MyVertex::new([1.0, -1.0, -1.0], [0.0, 1.0]),
            MyVertex::new([1.0, 1.0, 1.0], [1.0, 0.0]),
            MyVertex::new([1.0, -1.0, 1.0], [0.0, 0.0]),
            // Side
            MyVertex::new([1.0, 1.0, 1.0], [0.0, 0.0]),
            MyVertex::new([1.0, 1.0, -1.0], [0.0, 1.0]),
            MyVertex::new([-1.0, 1.0, -1.0], [1.0, 1.0]),
            MyVertex::new([1.0, 1.0, 1.0], [0.0, 0.0]),
            MyVertex::new([-1.0, 1.0, -1.0], [1.0, 1.0]),
            MyVertex::new([-1.0, 1.0, 1.0], [1.0, 0.0]),
            // Bottom
            MyVertex::new([-1.0, 1.0, 1.0], [0.0, 0.0]),
            MyVertex::new([-1.0, -1.0, 1.0], [1.0, 0.0]),
            MyVertex::new([1.0, -1.0, 1.0], [1.0, 1.0]),
            MyVertex::new([1.0, 1.0, 1.0], [0.0, 0.0]),
            MyVertex::new([-1.0, 1.0, 1.0], [0.0, 1.0]),
            MyVertex::new([1.0, -1.0, 1.0], [1.0, 1.0]),
        ];

        let vertex_buffer = Buffer::from_iter(
            memory_allocator,
            BufferCreateInfo {
                usage: BufferUsage::VERTEX_BUFFER,
                ..Default::default()
            },
            AllocationCreateInfo {
                memory_type_filter: MemoryTypeFilter::PREFER_DEVICE
                    | MemoryTypeFilter::HOST_SEQUENTIAL_WRITE,
                ..Default::default()
            },
            VERTICES,
        )
        .unwrap();

        let vs = vs::load(device.clone()).map_err(Validated::unwrap).unwrap();
        let fs = fs::load(device.clone()).map_err(Validated::unwrap).unwrap();

        let vs_entry_point = vs.entry_point("main").unwrap();
        let fs_entry_point = fs.entry_point("main").unwrap();

        let vertex_input_state = MyVertex::per_vertex().definition(&vs_entry_point).unwrap();

        let stages = [vs_entry_point, fs_entry_point]
            .into_iter()
            .map(|e| PipelineShaderStageCreateInfo::new(e))
            .collect();

        let layout = PipelineLayout::new(
            device.clone(),
            PipelineDescriptorSetLayoutCreateInfo::from_stages(&stages)
                .into_pipeline_layout_create_info(device.clone())
                .unwrap(),
        )
        .map_err(Validated::unwrap)
        .unwrap();

        let (framebuffers, viewport) = create_framebuffers(&swapchain_images, &render_pass);
        let subpass = Subpass::from(render_pass, 0).unwrap();

        let pipeline = GraphicsPipeline::new(
            device.clone(),
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
        .map_err(Validated::unwrap)
        .unwrap();

        let command_buffer_allocator = Arc::new(StandardCommandBufferAllocator::new(
            device.clone(),
            Default::default(),
        ));

        if cfg!(unix) {
            assert!(memory_exporter.is_valid(), "Memory is no longer valid");
        }

        Self {
            pipeline,
            command_buffer_allocator,
            queue,
            vertex_buffer,
            framebuffers,
            swapchain,
            swapchain_fences: Vec::new(),
            memory_exporter: memory_exporter,
        }

        //
        // let mut recreate_swapchain = false;
        // let mut previous_frame_end = Some(vulkano::sync::now(device.clone()).boxed());
        //
        // let descriptor_set_allocator = StandardDescriptorSetAllocator::new(device.clone());
        //
        // let layout = pipeline.layout().set_layouts().get(0).unwrap();
        // let sampler = Sampler::new(
        //     device.clone(),
        //     SamplerCreateInfo {
        //         mag_filter: Filter::Linear,
        //         min_filter: Filter::Linear,
        //         address_mode: [SamplerAddressMode::Repeat; 3],
        //         ..Default::default()
        //     },
        // )
        // .unwrap();
        //
        // let sync_image_set = PersistentDescriptorSet::new(
        //     &descriptor_set_allocator,
        //     layout.clone(),
        //     [WriteDescriptorSet::image_view_sampler(
        //         0, image_view, sampler,
        //     )],
        // )
        // .unwrap();
        //
        // event_loop.run(move |event, _, control_flow| {
        //     let window = surface.object().unwrap().downcast_ref::<Window>().unwrap();
        //     match event {
        //         Event::WindowEvent {
        //             event: WindowEvent::CloseRequested,
        //             ..
        //         } => *control_flow = ControlFlow::Exit,
        //         Event::WindowEvent {
        //             event: WindowEvent::Resized(_),
        //             ..
        //         } => recreate_swapchain = true,
        //         Event::RedrawEventsCleared => {
        //             let dimensions = window.inner_size();
        //             if dimensions.width == 0 || dimensions.height == 0 {
        //                 return;
        //             }
        //
        //             queue
        //                 .with(|mut q| unsafe {
        //                     q.submit_unchecked(
        //                         [SubmitInfo {
        //                             signal_semaphores: vec![SemaphoreSubmitInfo::semaphore(
        //                                 vk_end_sem.clone(),
        //                             )],
        //                             ..Default::default()
        //                         }],
        //                         None,
        //                     )
        //                 })
        //                 .unwrap();
        //
        //             queue
        //                 .with(|mut q| unsafe {
        //                     q.submit_unchecked(
        //                         [SubmitInfo {
        //                             wait_semaphores: vec![SemaphoreSubmitInfo::semaphore(
        //                                 vk_begin_sem.clone(),
        //                             )],
        //                             ..Default::default()
        //                         }],
        //                         None,
        //                     )
        //                 })
        //                 .unwrap();
        //
        //             previous_frame_end.as_mut().unwrap().cleanup_finished();
        //
        //             if recreate_swapchain {
        //                 let (new_swapchain, swapchain_images) =
        //                     match swapchain.recreate(SwapchainCreateInfo {
        //                         image_extent: dimensions.into(),
        //                         ..swapchain.create_info()
        //                     }) {
        //                         Err(SwapchainCreationError::ImageExtentNotSupported { .. }) => {
        //                             return;
        //                         }
        //                         r => r.unwrap(),
        //                     };
        //                 swapchain = new_swapchain;
        //                 framebuffers = window::create_framebuffers(
        //                     &swapchain_images,
        //                     &render_pass,
        //                     &mut viewport,
        //                     &mut perspective,
        //                 );
        //                 recreate_swapchain = false;
        //             }
        //
        //             let (image_num, suboptimal, acquire_future) =
        //                 match acquire_next_image(swapchain.clone(), None) {
        //                     Err(AcquireError::OutOfDate) => {
        //                         recreate_swapchain = true;
        //                         return;
        //                     }
        //                     r => r.unwrap(),
        //                 };
        //
        //             // Suboptimal means we can still draw, but should recreate the swapchain for next frame anyway
        //             if suboptimal {
        //                 recreate_swapchain = true;
        //             }
        //
        //             let mut builder = AutoCommandBufferBuilder::primary(
        //                 &command_buffer_allocator,
        //                 queue.queue_family_index(),
        //                 CommandBufferUsage::OneTimeSubmit,
        //             )
        //             .unwrap();
        //
        //             let t = app_timer.elapsed().as_millis() as f32 * 0.001;
        //             let mvp: Matrix4<f32> = perspective
        //                 * Matrix4::look_at_rh(
        //                     Point3 {
        //                         x: t.sin() * 10.0,
        //                         y: t.cos() * 10.0,
        //                         z: -3.0,
        //                     },
        //                     Point3 {
        //                         x: 0.0,
        //                         y: 0.0,
        //                         z: 0.0,
        //                     },
        //                     Vector3::unit_z(),
        //                 );
        //             let push_constants = vs::ty::FrameData {
        //                 time: t,
        //                 mvp: mvp.into(),
        //                 ..Default::default()
        //             };
        //
        //             builder
        //                 .begin_render_pass(
        //                     RenderPassBeginInfo {
        //                         clear_values: vec![Some([0.0, 0.0, 1.0, 1.0].into())],
        //                         ..RenderPassBeginInfo::framebuffer(
        //                             framebuffers[image_num as usize].clone(),
        //                         )
        //                     },
        //                     SubpassContents::Inline,
        //                 )
        //                 .unwrap()
        //                 .set_viewport(0, [viewport.clone()])
        //                 .bind_pipeline_graphics(pipeline.clone())
        //                 .bind_vertex_buffers(0, vertex_buffer.clone())
        //                 .push_constants(pipeline.layout().clone(), 0, push_constants)
        //                 .bind_descriptor_sets(
        //                     PipelineBindPoint::Graphics,
        //                     pipeline.layout().clone(),
        //                     0,
        //                     sync_image_set.clone(),
        //                 )
        //                 .draw(VERTICES.len().try_into().unwrap(), 1, 0, 0)
        //                 .unwrap()
        //                 .end_render_pass()
        //                 .unwrap();
        //
        //             let command_buffer = builder.build().unwrap();
        //
        //             match previous_frame_end
        //                 .take()
        //                 .unwrap()
        //                 .join(acquire_future)
        //                 .then_execute(queue.clone(), command_buffer)
        //                 .unwrap()
        //                 .then_swapchain_present(
        //                     queue.clone(),
        //                     SwapchainPresentInfo::swapchain_image_index(
        //                         swapchain.clone(),
        //                         image_num,
        //                     ),
        //                 )
        //                 .then_signal_fence_and_flush()
        //             {
        //                 Ok(f) => previous_frame_end = Some(f.boxed()),
        //                 Err(FlushError::OutOfDate) => {
        //                     recreate_swapchain = true;
        //                     previous_frame_end = Some(vulkano::sync::now(device.clone()).boxed());
        //                 }
        //                 Err(e) => panic!("Failed to flush future: {:?}", e),
        //             }
        //         }
        //         _ => (),
        //     }
        // });
    }

    pub fn draw(&mut self, time: f32) {
        let mut builder = AutoCommandBufferBuilder::primary(
            self.command_buffer_allocator.clone(),
            self.queue.queue_family_index(),
            CommandBufferUsage::OneTimeSubmit,
        )
        .map_err(Validated::unwrap)
        .unwrap();

        let (image_index, framebuffer_suboptimal, acquire_future) =
            acquire_next_image(self.swapchain.clone(), Some(Duration::from_millis(20)))
                .map_err(Validated::unwrap)
                .unwrap();

        if framebuffer_suboptimal {
            unimplemented!()
        }

        builder
            .begin_render_pass(
                RenderPassBeginInfo {
                    clear_values: vec![Some([0.0, 0.0, 0.0, 1.0].into())],
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
            .unwrap();

        let image_extent = self.framebuffers[image_index as usize].extent();

        let perspective = Matrix4::from(PerspectiveFov {
            fovy: Deg(45.0).into(),
            aspect: image_extent[0] as f32 / image_extent[1] as f32,
            near: 0.1,
            far: 100.0,
        });
        let mvp = perspective
            * Matrix4::look_at_rh(
                Point3 {
                    x: time.sin() * 10.0,
                    y: time.cos() * 10.0,
                    z: -3.0,
                },
                Point3 {
                    x: 0.0,
                    y: 0.0,
                    z: 0.0,
                },
                Vector3::unit_z(),
            );
        let push_constants = vs::FrameData {
            time: time.into(),
            mvp: mvp.into(),
        };

        // let push_constants = PushConstant {
        //     time: params.time.into(),
        //     mvp: mvp.to_cols_array_2d(),
        // };

        builder
            .push_constants(pipeline_layout.clone(), 0, push_constants)
            .unwrap();

        unsafe {
            builder.draw(36, 1, 0, 0).unwrap();
        }

        builder.end_render_pass(Default::default()).unwrap();

        let command_buffer = builder.build().map_err(Validated::unwrap).unwrap();

        if let Some(future) = self.swapchain_fences.get_mut(image_index as usize) {
            let moved_future = future.take().unwrap();
            *future = Some(
                moved_future
                    .join(acquire_future)
                    .then_execute(self.queue.clone(), command_buffer)
                    .unwrap()
                    .boxed(),
            );
        } else {
            self.swapchain_fences.push(Some(
                acquire_future
                    .then_execute(self.queue.clone(), command_buffer)
                    .unwrap()
                    .boxed(),
            ));
        }
    }
}
