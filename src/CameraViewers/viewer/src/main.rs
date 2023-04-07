use std::{sync::Arc, time::Instant};

use cgmath::{Matrix4, Point3, SquareMatrix, Vector3};
// use platform::get_allowed_external_semaphore_handle_types;
use vulkano::{
    buffer::{BufferUsage, CpuAccessibleBuffer},
    command_buffer::{
        allocator::StandardCommandBufferAllocator, AutoCommandBufferBuilder, CommandBufferUsage,
        RenderPassBeginInfo, SubpassContents,
    },
    device::{Device, DeviceCreateInfo, QueueCreateInfo},
    format::Format,
    image::{ImageCreateFlags, ImageUsage},
    memory::{allocator::StandardMemoryAllocator},
    pipeline::{
        graphics::{
            rasterization::{CullMode, FrontFace, RasterizationState},
            vertex_input::BuffersDefinition,
            viewport::{Viewport, ViewportState},
        },
        GraphicsPipeline, Pipeline, StateMode,
    },
    render_pass::Subpass,
    swapchain::{
        acquire_next_image, AcquireError, SwapchainCreateInfo, SwapchainCreationError,
        SwapchainPresentInfo,
    },
    sync::{
        ExternalSemaphoreHandleTypes,
        FlushError, GpuFuture, Semaphore, SemaphoreCreateInfo, SemaphoreError,
    },
};
use vulkano_win::VkSurfaceBuild;
use winit::{
    dpi::PhysicalSize,
    event::{Event, WindowEvent},
    event_loop::{ControlFlow, EventLoop},
    window::{Window, WindowBuilder},
};

use crate::{
    platform::{
        get_external_memory_type, get_external_semaphore_type,
        print_semaphore_handle, print_memory_handle,
    },
    window::find_physical_device, external_image::ExternalImage,
};

mod platform;
mod window;
mod external_image;

fn create_external_semaphore(
    device: Arc<Device>,
    handle_type: ExternalSemaphoreHandleTypes,
) -> Result<Arc<Semaphore>, SemaphoreError> {
    Ok(Arc::new(Semaphore::new(
        device,
        SemaphoreCreateInfo {
            export_handle_types: handle_type,
            ..Default::default()
        },
    )?))
}

const WIDTH: u32 = 800;
const HEIGHT: u32 = 600;

fn main() {
    let app_timer = Instant::now();
    println!("Hello, triangle!");

    let instance = window::create_instance();
    let _debug_messenger = unsafe { window::create_debug_messenger(&instance) };

    let event_loop = EventLoop::new();
    let surface = WindowBuilder::new()
        .with_title("Viewer")
        .with_inner_size(PhysicalSize::new(WIDTH, HEIGHT))
        .build_vk_surface(&event_loop, instance.clone())
        .unwrap();

    let (physical_device, queue_family_index) = find_physical_device(&instance, &surface).unwrap();

    if cfg!(debug_assertions) {
        println!(
            "Using {} and vulkan {:?}",
            physical_device.properties().device_name,
            instance.api_version()
        );
    }

    let (device, mut queue) = Device::new(
        physical_device.clone(),
        DeviceCreateInfo {
            enabled_extensions: window::DEVICE_EXTENSIONS,
            queue_create_infos: vec![QueueCreateInfo {
                queue_family_index,
                ..Default::default()
            }],
            ..Default::default()
        },
    )
    .unwrap();
    let queue = queue.next().unwrap();

    let (mut swapchain, swapchain_images) = window::create_swapchain(&device, &surface).unwrap();

    let memory_allocator = StandardMemoryAllocator::new_default(device.clone());

    // Logic taken from https://github.com/vulkano-rs/vulkano/blob/master/examples/src/bin/gl-interop.rs
    // (which is based on https://github.com/KhronosGroup/Vulkan-Samples/blob/master/samples/extensions/open_gl_interop/open_gl_interop.cpp)

    let semaphore_handle_type = get_external_semaphore_type(&physical_device).unwrap();
    let memory_handle_type = get_external_memory_type(&physical_device, BufferUsage { transfer_src: true, transfer_dst: true, storage_texel_buffer: true, ..Default::default() }).unwrap();

    let acquire_sem =
        create_external_semaphore(device.clone(), semaphore_handle_type.into()).unwrap();
    let release_sem =
        create_external_semaphore(device.clone(), semaphore_handle_type.into()).unwrap();

    print_semaphore_handle("OGL_begin", &release_sem, semaphore_handle_type);
    print_semaphore_handle("OGL_end", &acquire_sem, semaphore_handle_type);

    // TODO: Make a version that works on Windows (POSIX file descriptor handles only works on Unix)
    let image = ExternalImage::new(
        &memory_allocator,
        vulkano::image::ImageDimensions::Dim2d {
            width: WIDTH,
            height: HEIGHT,
            array_layers: 1,
        },
        Format::R16G16B16A16_UNORM,
        ImageUsage {
            sampled: true,
            transfer_src: true,
            transfer_dst: true,
            ..Default::default()
        },
        ImageCreateFlags {
            mutable_format: true,
            ..Default::default()
        },
        [queue.queue_family_index()],
        memory_handle_type,
    )
    .unwrap();

    print_memory_handle("OGL_buffer", &image, memory_handle_type);

    mod vs {
        vulkano_shaders::shader! {
            ty: "vertex",
            src: "
            #version 460
            layout(location = 0) in vec3 position;
            layout(push_constant) uniform FrameData {
                float time;
                mat4 mvp;
            } frame_data;
            layout(location = 0) out vec3 colour;
            void main() {
                colour = position * 0.5 + 0.5;
                gl_Position = frame_data.mvp * vec4(position, 1.0);
            }
            ",
            types_meta: {
                use bytemuck::{Pod, Zeroable};
                #[derive(Clone, Copy, Default, Zeroable, Pod)]
            }
        }
    }

    mod fs {
        vulkano_shaders::shader! {
            ty: "fragment",
            src: "
            #version 460

            layout(location = 0) in vec3 colour;
            layout(location = 0) out vec4 frag_colour;

            void main() {
                frag_colour = vec4(colour, 1.0);
            }
            "
        }
    }

    let vs = vs::load(device.clone()).unwrap();
    let fs = fs::load(device.clone()).unwrap();

    let render_pass = vulkano::single_pass_renderpass!(
        device.clone(),
        attachments: {
            color: {
                load: Clear,
                store: Store,
                format: swapchain.image_format(),
                samples: 1,
            }
        },
        pass: {
            color: [color],
            depth_stencil: {}
        }
    )
    .unwrap();

    // Cube vertex data
    const VERTICES: [window::Vertex; 36] = [
        window::Vertex::new([-1.0, -1.0, -1.0]),
        window::Vertex::new([-1.0, -1.0, 1.0]),
        window::Vertex::new([-1.0, 1.0, 1.0]),
        window::Vertex::new([1.0, 1.0, -1.0]),
        window::Vertex::new([-1.0, -1.0, -1.0]),
        window::Vertex::new([-1.0, 1.0, -1.0]),
        window::Vertex::new([1.0, -1.0, 1.0]),
        window::Vertex::new([-1.0, -1.0, -1.0]),
        window::Vertex::new([1.0, -1.0, -1.0]),
        window::Vertex::new([1.0, 1.0, -1.0]),
        window::Vertex::new([1.0, -1.0, -1.0]),
        window::Vertex::new([-1.0, -1.0, -1.0]),
        window::Vertex::new([-1.0, -1.0, -1.0]),
        window::Vertex::new([-1.0, 1.0, 1.0]),
        window::Vertex::new([-1.0, 1.0, -1.0]),
        window::Vertex::new([1.0, -1.0, 1.0]),
        window::Vertex::new([-1.0, -1.0, 1.0]),
        window::Vertex::new([-1.0, -1.0, -1.0]),
        window::Vertex::new([-1.0, 1.0, 1.0]),
        window::Vertex::new([-1.0, -1.0, 1.0]),
        window::Vertex::new([1.0, -1.0, 1.0]),
        window::Vertex::new([1.0, 1.0, 1.0]),
        window::Vertex::new([1.0, -1.0, -1.0]),
        window::Vertex::new([1.0, 1.0, -1.0]),
        window::Vertex::new([1.0, -1.0, -1.0]),
        window::Vertex::new([1.0, 1.0, 1.0]),
        window::Vertex::new([1.0, -1.0, 1.0]),
        window::Vertex::new([1.0, 1.0, 1.0]),
        window::Vertex::new([1.0, 1.0, -1.0]),
        window::Vertex::new([-1.0, 1.0, -1.0]),
        window::Vertex::new([1.0, 1.0, 1.0]),
        window::Vertex::new([-1.0, 1.0, -1.0]),
        window::Vertex::new([-1.0, 1.0, 1.0]),
        window::Vertex::new([1.0, 1.0, 1.0]),
        window::Vertex::new([-1.0, 1.0, 1.0]),
        window::Vertex::new([1.0, -1.0, 1.0]),
    ];

    let vertex_buffer = CpuAccessibleBuffer::from_data(
        &memory_allocator,
        BufferUsage {
            vertex_buffer: true,
            ..Default::default()
        },
        false,
        VERTICES,
    )
    .unwrap();

    let pipeline = GraphicsPipeline::start()
        .render_pass(Subpass::from(render_pass.clone(), 0).unwrap())
        .vertex_input_state(BuffersDefinition::new().vertex::<window::Vertex>())
        // .input_assembly_state(InputAssemblyState::default())
        .vertex_shader(vs.entry_point("main").unwrap(), ())
        .fragment_shader(fs.entry_point("main").unwrap(), ())
        .viewport_state(ViewportState::viewport_dynamic_scissor_irrelevant())
        .rasterization_state(RasterizationState {
            cull_mode: StateMode::Fixed(CullMode::Back),
            front_face: StateMode::Fixed(FrontFace::Clockwise),
            ..Default::default()
        })
        .build(device.clone())
        .unwrap();

    let mut viewport = Viewport {
        origin: [0.0, 0.0],
        dimensions: [0.0, 0.0],
        depth_range: 0.0..1.0,
    };

    let mut perspective = Matrix4::identity();
    let mut framebuffers = window::create_framebuffers(
        &swapchain_images,
        &render_pass,
        &mut viewport,
        &mut perspective,
    );

    if cfg!(debug_assertions) {
        println!(
            "Setup took {} milliseconds to complete",
            app_timer.elapsed().as_millis()
        );
    }

    let mut recreate_swapchain = false;
    let mut previous_frame_end = Some(vulkano::sync::now(device.clone()).boxed());

    let command_buffer_allocator =
        StandardCommandBufferAllocator::new(device.clone(), Default::default());

    event_loop.run(move |event, _, control_flow| {
        let window = surface.object().unwrap().downcast_ref::<Window>().unwrap();
        match event {
            Event::WindowEvent {
                event: WindowEvent::CloseRequested,
                ..
            } => *control_flow = ControlFlow::Exit,
            Event::WindowEvent {
                event: WindowEvent::Resized(_),
                ..
            } => recreate_swapchain = true,
            Event::RedrawEventsCleared => {
                let dimensions = window.inner_size();
                if dimensions.width == 0 || dimensions.height == 0 {
                    return;
                }

                previous_frame_end.as_mut().unwrap().cleanup_finished();

                if recreate_swapchain {
                    let (new_swapchain, swapchain_images) =
                        match swapchain.recreate(SwapchainCreateInfo {
                            image_extent: dimensions.into(),
                            ..swapchain.create_info()
                        }) {
                            Err(SwapchainCreationError::ImageExtentNotSupported { .. }) => return,
                            r => r.unwrap(),
                        };
                    swapchain = new_swapchain;
                    framebuffers = window::create_framebuffers(
                        &swapchain_images,
                        &render_pass,
                        &mut viewport,
                        &mut perspective,
                    );
                    recreate_swapchain = false;
                }

                let (image_num, suboptimal, acquire_future) =
                    match acquire_next_image(swapchain.clone(), None) {
                        Err(AcquireError::OutOfDate) => {
                            recreate_swapchain = true;
                            return;
                        }
                        r => r.unwrap(),
                    };

                // Suboptimal means we can still draw, but should recreate the swapchain for next frame anyway
                if suboptimal {
                    recreate_swapchain = true;
                }

                let mut builder = AutoCommandBufferBuilder::primary(
                    &command_buffer_allocator,
                    queue.queue_family_index(),
                    CommandBufferUsage::OneTimeSubmit,
                )
                .unwrap();

                let t = app_timer.elapsed().as_millis() as f32 * 0.001;
                let mvp: Matrix4<f32> = perspective
                    * Matrix4::look_at_rh(
                        Point3 {
                            x: t.sin() * 10.0,
                            y: t.cos() * 10.0,
                            z: -3.0,
                        },
                        Point3 {
                            x: 0.0,
                            y: 0.0,
                            z: 0.0,
                        },
                        Vector3::unit_z(),
                    );
                let push_constants = vs::ty::FrameData {
                    time: t,
                    mvp: mvp.into(),
                    ..Default::default()
                };

                builder
                    .begin_render_pass(
                        RenderPassBeginInfo {
                            clear_values: vec![Some([0.0, 0.0, 1.0, 1.0].into())],
                            ..RenderPassBeginInfo::framebuffer(
                                framebuffers[image_num as usize].clone(),
                            )
                        },
                        SubpassContents::Inline,
                    )
                    .unwrap()
                    .set_viewport(0, [viewport.clone()])
                    .bind_pipeline_graphics(pipeline.clone())
                    .bind_vertex_buffers(0, vertex_buffer.clone())
                    .push_constants(pipeline.layout().clone(), 0, push_constants)
                    .draw(VERTICES.len().try_into().unwrap(), 1, 0, 0)
                    .unwrap()
                    .end_render_pass()
                    .unwrap();

                let command_buffer = builder.build().unwrap();

                match previous_frame_end
                    .take()
                    .unwrap()
                    .join(acquire_future)
                    .then_execute(queue.clone(), command_buffer)
                    .unwrap()
                    .then_swapchain_present(
                        queue.clone(),
                        SwapchainPresentInfo::swapchain_image_index(swapchain.clone(), image_num),
                    )
                    .then_signal_fence_and_flush()
                {
                    Ok(f) => previous_frame_end = Some(f.boxed()),
                    Err(FlushError::OutOfDate) => {
                        recreate_swapchain = true;
                        previous_frame_end = Some(vulkano::sync::now(device.clone()).boxed());
                    }
                    Err(e) => panic!("Failed to flush future: {:?}", e),
                }
            }
            _ => (),
        }
    });
}

#[test]
fn handle_format_logic_test() {
    // Difficult to check the actual functions, so we just check the logic instead
    assert_eq!(format!("{:016x}", 1usize), "0000000000000001");
    assert_eq!(format!("{:016x}", 1usize << 1), "0000000000000002");
    assert_eq!(format!("{:016x}", 15usize), "000000000000000f");
    assert_eq!(format!("{:016x}", 16usize), "0000000000000010");
    assert_eq!(format!("{:016x}", usize::MAX), "ffffffffffffffff");
}
