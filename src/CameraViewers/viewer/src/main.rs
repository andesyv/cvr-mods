#![feature(unix_socket_ancillary_data)]

use crate::platform::IPCChannel;
use std::env::args;
use std::path::Path;
use std::sync::Arc;
use uuid::{Uuid, uuid};
// use platform::get_allowed_external_semaphore_handle_types;
use crate::render_context::RenderContextCreationInfo;
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

fn parse_args(args: &[String]) -> RenderContextCreationInfo {
    if let Some((driver, devices)) = args
        .get(1)
        .map(|s| parse_driver_and_devices_uuids(&s[..]))
        .flatten()
    {
        let channel = args
            .get(2)
            .map(|s| find_channel_from_args(&s[..]))
            .flatten();
        RenderContextCreationInfo {
            driver: Some(driver),
            devices,
            owner_channel: channel,
        }
    } else {
        Default::default()
    }
}

#[cfg(unix)]
fn find_channel_from_args(possible_socket_path: &str) -> Option<IPCChannel> {
    let socket_path = Path::new(possible_socket_path);
    if socket_path
        .extension()
        .map(|e| e == "sock")
        .unwrap_or(false)
        && socket_path.exists()
    {
        println!("Using passed socket {}", possible_socket_path);
        return Some(IPCChannel::new(socket_path).expect("Failed to connect to socket"));
    }
    None
}

fn parse_driver_and_devices_uuids(configuration: &str) -> Option<(Uuid, Vec<Uuid>)> {
    let configuration = configuration.strip_prefix("\"").map(|s|s.strip_suffix("\"")).flatten().unwrap_or(configuration);
    let configuration = configuration.strip_prefix("driver: ")?;
    let (driver_uuid_str, devices_uuids_str) = configuration.split_once(", devices: ")?;
    let devices_uuids = devices_uuids_str
        .split(',')
        .filter_map(|s| s.parse().ok())
        .collect();
    Some((driver_uuid_str.parse().ok()?, devices_uuids))
}

fn main() {
    println!("Hello, viewer!");

    let args: Vec<_> = args().collect();
    let configuration = parse_args(&args[..]);

    let event_loop = EventLoop::new().unwrap();
    let mut window = Window::new(configuration);
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

#[test]
fn parse_driver_and_devices_uuids_test() {
    let sut = parse_driver_and_devices_uuids(
        "\
driver: 4991c5f7-db3a-4c59-91c4-3b973c713ce7, \
devices: \
edf48757-4a29-491d-8958-9deb78cc68e1",
    );
    assert_eq!(
        sut,
        Some((
            uuid!("4991c5f7-db3a-4c59-91c4-3b973c713ce7"),
            vec![uuid!("edf48757-4a29-491d-8958-9deb78cc68e1")]
        ))
    );

    let sut = parse_driver_and_devices_uuids(
        "\
driver: 4991c5f7-db3a-4c59-91c4-3b973c713ce7, \
devices: \
edf48757-4a29-491d-8958-9deb78cc68e1,\
62c4c982-fc9a-4414-8510-01af13baee8c,\
7774650b-2f01-4af7-98d3-14bba80e702d",
    );
    assert_eq!(
        sut,
        Some((
            uuid!("4991c5f7-db3a-4c59-91c4-3b973c713ce7"),
            vec![
                uuid!("edf48757-4a29-491d-8958-9deb78cc68e1"),
                uuid!("62c4c982-fc9a-4414-8510-01af13baee8c"),
                uuid!("7774650b-2f01-4af7-98d3-14bba80e702d")
            ]
        ))
    );
}
