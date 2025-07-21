use std::sync::Arc;

// use platform::get_allowed_external_semaphore_handle_types;
use crate::window::Window;
use vulkano::sync::semaphore::{ExternalSemaphoreHandleTypes, Semaphore, SemaphoreCreateInfo};
use vulkano::{Validated, VulkanError, device::Device};
use winit::event_loop::EventLoop;

mod external_image;
mod platform;
mod render_context;
mod window;

fn create_external_semaphore(
    device: Arc<Device>,
    handle_types: ExternalSemaphoreHandleTypes,
) -> Result<Arc<Semaphore>, VulkanError> {
    Ok(Arc::new(
        Semaphore::new(
            device,
            SemaphoreCreateInfo {
                export_handle_types: handle_types,
                ..Default::default()
            },
        )
        .map_err(Validated::unwrap)?,
    ))
}

const WIDTH: u32 = 800;
const HEIGHT: u32 = 600;

fn main() {
    println!("Hello, triangle!");
    let event_loop = EventLoop::new().unwrap();
    let mut window = Window::default();
    event_loop.run_app(&mut window).unwrap();
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
