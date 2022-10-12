use std::sync::Arc;

use vulkano::{
    device::{
        physical::{PhysicalDevice, PhysicalDeviceType, QueueFamily},
        Device, DeviceExtensions,
    },
    image::{ImageUsage, SwapchainImage, ImageAccess, view::ImageView},
    instance::{
        debug::{
            DebugUtilsMessageSeverity, DebugUtilsMessageType, DebugUtilsMessenger,
            DebugUtilsMessengerCreateInfo, Message,
        },
        Instance, InstanceCreateInfo, InstanceExtensions,
    },
    swapchain::{Surface, Swapchain, SwapchainCreateInfo},
    Version, render_pass::{RenderPass, Framebuffer, FramebufferCreateInfo}, pipeline::graphics::viewport::Viewport,
};
use vulkano_win::required_extensions;
use winit::window::Window;

pub const DEVICE_EXTENSIONS: DeviceExtensions = DeviceExtensions {
    khr_swapchain: true,
    ..DeviceExtensions::none()
};

pub fn create_instance() -> Arc<Instance> {
    Instance::new(InstanceCreateInfo {
        application_name: Some("Viewer".to_string()),
        application_version: Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        enabled_extensions: InstanceExtensions {
            ext_debug_utils: true,
            ..required_extensions()
        },
        enabled_layers: vec![
            // "VK_LAYER_LUNARG_standard_validation".to_string(), // The best one
            "VK_LAYER_KHRONOS_validation".to_string(), // Likely more available
        ],
        enumerate_portability: true, // Allow non-conformant vulkan devices (e.g. MoltenVK)
        ..Default::default()
    })
    .unwrap()
}

#[cfg(debug_assertions)]
fn debug_message_callback(message: &Message) {
    match message.severity {
        DebugUtilsMessageSeverity {
            information: true,
            verbose: false,
            ..
        } => println!("Vulkan Info: {}", message.description),
        DebugUtilsMessageSeverity { verbose: false, .. } => println!(
            "Vulkan Error (Type: {:?}): {}",
            message.ty, message.description
        ),
        _ => (),
    };
}

#[cfg(not(debug_assertions))]
fn debug_message_callback(message: &Message) {
    match message.severity {
        DebugUtilsMessageSeverity {
            information: false,
            verbose: false,
            ..
        } => println!("Error (Type: {:?}): {}", message.ty, message.description),
        _ => (),
    };
}

pub unsafe fn create_debug_messenger(instance: &Arc<Instance>) -> DebugUtilsMessenger {
    DebugUtilsMessenger::new(
        instance.clone(),
        DebugUtilsMessengerCreateInfo {
            message_severity: DebugUtilsMessageSeverity {
                error: true,
                warning: true,
                information: true,
                verbose: true,
            },
            message_type: DebugUtilsMessageType {
                general: true,
                validation: true,
                performance: true,
            },
            ..DebugUtilsMessengerCreateInfo::user_callback(Arc::new(debug_message_callback))
        },
    )
    .unwrap()
}

pub fn find_physical_device<'a>(
    instance: &'a Arc<Instance>,
    surface: &Surface<Window>,
) -> Option<(PhysicalDevice<'a>, QueueFamily<'a>)> {
    PhysicalDevice::enumerate(&instance)
        .filter_map(|d| {
            if d.supported_extensions().is_superset_of(&DEVICE_EXTENSIONS) {
                d.queue_families()
                    .filter(|q| {
                        q.supports_graphics() && q.supports_surface(&surface).unwrap_or(false)
                    })
                    .next()
                    .and_then(|i| Some((d, i)))
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
    surface: &Arc<Surface<Window>>,
) -> Option<(
    Arc<Swapchain<Window>>,
    Vec<Arc<SwapchainImage<Window>>>,
)> {
    let surface_capabilities = device
        .physical_device()
        .surface_capabilities(surface, Default::default())
        .unwrap();

    let image_format = device
        .physical_device()
        .surface_formats(&surface, Default::default())
        .unwrap()
        .first()
        .and_then(|t| Some(t.0));

    Swapchain::new(
        device.clone(),
        surface.clone(),
        SwapchainCreateInfo {
            min_image_count: surface_capabilities.min_image_count,
            image_format,
            image_extent: surface.window().inner_size().into(),
            image_usage: ImageUsage {
                color_attachment: true,
                ..ImageUsage::none()
            },
            ..Default::default()
        },
    )
    .ok()
}

pub fn create_framebuffers(
  images: &[Arc<SwapchainImage<Window>>],
  render_pass: &Arc<RenderPass>,
  viewport: &mut Viewport,
) -> Vec<Arc<Framebuffer>> {
  let dimensions = images[0].dimensions().width_height();
  viewport.dimensions = [dimensions[0] as f32, dimensions[1] as f32];

  images
      .iter()
      .map(|img| {
          let view = ImageView::new_default(img.clone()).unwrap();
          Framebuffer::new(
              render_pass.clone(),
              FramebufferCreateInfo {
                  attachments: vec![view],
                  ..Default::default()
              },
          )
          .unwrap()
      })
      .collect()
}
