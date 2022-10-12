use std::time::Instant;

use vulkano::{
    command_buffer::{
        AutoCommandBufferBuilder, CommandBufferUsage, RenderPassBeginInfo, SubpassContents,
    },
    device::{Device, DeviceCreateInfo, QueueCreateInfo},
    pipeline::graphics::viewport::Viewport,
    swapchain::{acquire_next_image, AcquireError, SwapchainCreateInfo, SwapchainCreationError},
    sync::{FlushError, GpuFuture},
};
use vulkano_win::VkSurfaceBuild;
use winit::{
    dpi::PhysicalSize,
    event::{Event, WindowEvent},
    event_loop::{ControlFlow, EventLoop},
    window::WindowBuilder,
};

use crate::window::find_physical_device;

mod window;

fn main() {
    let app_timer = Instant::now();
    println!("Hello, triangle!");

    let instance = window::create_instance();
    let _debug_messenger = unsafe { window::create_debug_messenger(&instance) };

    let event_loop = EventLoop::new();
    let surface = WindowBuilder::new()
        .with_title("Viewer")
        .with_inner_size(PhysicalSize::new(800, 600))
        .build_vk_surface(&event_loop, instance.clone())
        .unwrap();

    let (physical_device, queue_family) = find_physical_device(&instance, &surface).unwrap();

    if cfg!(debug_assertions) {
        println!(
            "Using {} and vulkan {:?}",
            physical_device.properties().device_name,
            instance.api_version()
        );
    }

    let (device, mut queue) = Device::new(
        physical_device,
        DeviceCreateInfo {
            enabled_extensions: window::DEVICE_EXTENSIONS,
            queue_create_infos: vec![QueueCreateInfo::family(queue_family)],
            ..Default::default()
        },
    )
    .unwrap();
    let queue = queue.next().unwrap();

    let (mut swapchain, swapchain_images) = window::create_swapchain(&device, &surface).unwrap();

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

    let mut viewport = Viewport {
        origin: [0.0, 0.0],
        dimensions: [0.0, 0.0],
        depth_range: 0.0..1.0,
    };

    let mut framebuffers =
        window::create_framebuffers(&swapchain_images, &render_pass, &mut viewport);

    if cfg!(debug_assertions) {
        println!(
            "Setup took {} milliseconds to complete",
            app_timer.elapsed().as_millis()
        );
    }

    let mut recreate_swapchain = false;
    let mut previous_frame_end = Some(vulkano::sync::now(device.clone()).boxed());

    event_loop.run(move |event, _, control_flow| {
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
                let dimensions = surface.window().inner_size();
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
                    framebuffers =
                        window::create_framebuffers(&swapchain_images, &render_pass, &mut viewport);
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
                    device.clone(),
                    queue.family(),
                    CommandBufferUsage::OneTimeSubmit,
                )
                .unwrap();

                // Some random light show
                let t = app_timer.elapsed().as_millis() as f32 * 0.001;
                let a = (t * 0.3).fract();
                let b = 1.0 - (t * 0.1).fract();
                let c = (a * b * 2.3).fract();
                let clear_color = [a, b, c, 1.0];

                builder
                    .begin_render_pass(
                        RenderPassBeginInfo {
                            clear_values: vec![Some(clear_color.into())],
                            ..RenderPassBeginInfo::framebuffer(framebuffers[image_num].clone())
                        },
                        SubpassContents::Inline,
                    )
                    .unwrap()
                    .set_viewport(0, [viewport.clone()])
                    .end_render_pass()
                    .unwrap();

                let command_buffer = builder.build().unwrap();

                match previous_frame_end
                    .take()
                    .unwrap()
                    .join(acquire_future)
                    .then_execute(queue.clone(), command_buffer)
                    .unwrap()
                    .then_swapchain_present(queue.clone(), swapchain.clone(), image_num)
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
