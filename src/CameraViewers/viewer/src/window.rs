use std::sync::Arc;

use bytemuck::{Pod, Zeroable};
use cgmath::{Deg, Matrix4, PerspectiveFov};
use vulkano::{
    device::{
        physical::{PhysicalDevice, PhysicalDeviceType},
        Device, DeviceExtensions, QueueFlags,
    },
    image::{view::ImageView, ImageAccess, ImageUsage, SwapchainImage},
    impl_vertex,
    instance::{
        debug::{
            DebugUtilsMessageSeverity, DebugUtilsMessageType, DebugUtilsMessenger,
            DebugUtilsMessengerCreateInfo, Message,
        },
        Instance, InstanceCreateInfo, InstanceExtensions,
    },
    pipeline::graphics::viewport::Viewport,
    render_pass::{Framebuffer, FramebufferCreateInfo, RenderPass},
    swapchain::{Surface, Swapchain, SwapchainCreateInfo},
    Version, VulkanLibrary,
};
use vulkano_win::required_extensions;
use winit::window::Window;

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

pub const INSTANCE_EXTENSIONS: InstanceExtensions = InstanceExtensions {
    ext_debug_utils: true,
    khr_get_physical_device_properties2: true,
    khr_external_memory_capabilities: true,
    khr_external_semaphore_capabilities: true,
    khr_external_fence_capabilities: true,
    ..InstanceExtensions::empty()
};

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Zeroable, Pod)]
pub struct Vertex {
    position: [f32; 3],
    tex_coord: [f32; 2]
}

impl Vertex {
    pub const fn new(position: [f32; 3], tex_coord: [f32; 2]) -> Self {
        Vertex { position, tex_coord }
    }
}

impl_vertex!(Vertex, position, tex_coord);

pub fn create_instance() -> Arc<Instance> {
    let library = VulkanLibrary::new().unwrap();
    Instance::new(
        library.clone(),
        InstanceCreateInfo {
            application_name: Some("Viewer".to_string()),
            application_version: Version {
                major: 1,
                minor: 0,
                patch: 0,
            },
            enabled_extensions: INSTANCE_EXTENSIONS.union(&required_extensions(&library)),
            enabled_layers: vec![
                // "VK_LAYER_LUNARG_standard_validation".to_string(), // The best one
                "VK_LAYER_KHRONOS_validation".to_string(), // Likely more available
            ],
            enumerate_portability: true, // Allow non-conformant vulkan devices (e.g. MoltenVK)
            ..Default::default()
        },
    )
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
                ..Default::default()
            },
            message_type: DebugUtilsMessageType {
                general: true,
                validation: true,
                performance: true,
                ..Default::default()
            },
            ..DebugUtilsMessengerCreateInfo::user_callback(Arc::new(debug_message_callback))
        },
    )
    .unwrap()
}

pub fn find_physical_device<'a>(
    instance: &'a Arc<Instance>,
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
                        q.queue_flags.intersects(&QueueFlags {
                            graphics: true,
                            ..Default::default()
                        }) && d.surface_support(i as u32, &surface).unwrap_or(false)
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
) -> Option<(Arc<Swapchain>, Vec<Arc<SwapchainImage>>)> {
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
            image_extent: surface
                .object()
                .unwrap()
                .downcast_ref::<Window>()
                .unwrap()
                .inner_size()
                .into(),
            image_usage: ImageUsage {
                color_attachment: true,
                ..ImageUsage::empty()
            },
            ..Default::default()
        },
    )
    .ok()
}

pub fn create_framebuffers(
    images: &[Arc<SwapchainImage>],
    render_pass: &Arc<RenderPass>,
    viewport: &mut Viewport,
    perspective: &mut Matrix4<f32>,
) -> Vec<Arc<Framebuffer>> {
    let dimensions = images[0].dimensions().width_height();
    viewport.dimensions = [dimensions[0] as f32, dimensions[1] as f32];

    *perspective = Matrix4::from(PerspectiveFov {
        fovy: Deg(45.0).into(),
        aspect: viewport.dimensions[0] / viewport.dimensions[1],
        near: 0.1,
        far: 100.0,
    });

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
