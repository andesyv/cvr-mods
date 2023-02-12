use std::{ffi::c_void, sync::Arc};
use vulkano::{
    device::physical::PhysicalDevice,
    sync::{ExternalSemaphoreHandleType, ExternalSemaphoreInfo, Semaphore},
};

// #[cfg(windows)]
// pub const fn get_allowed_external_semaphore_handle_types() -> ExternalSemaphoreHandleTypes {
//     ExternalSemaphoreHandleTypes {
//         opaque_win32: false,
//         opaque_win32_kmt: true,
//         ..ExternalSemaphoreHandleTypes::none()
//     }
// }

// #[cfg(unix)]
// pub const fn get_allowed_external_semaphore_handle_types() -> ExternalSemaphoreHandleTypes {
//     ExternalSemaphoreHandleTypes::posix()
// }

// #[cfg(windows)]
// pub const fn get_allowed_external_memory_handle_types() -> ExternalMemoryHandleTypes {
//     ExternalMemoryHandleTypes {
//         opaque_win32: false,
//         opaque_win32_kmt: true,
//         ..ExternalMemoryHandleTypes::none()
//     }
// }

// #[cfg(unix)]
// pub const fn get_allowed_external_memory_handle_types() -> ExternalMemoryHandleTypes {
//     ExternalMemoryHandleTypes::posix()
// }

pub fn get_external_semaphore_type(
    physical_device: &PhysicalDevice,
) -> Option<ExternalSemaphoreHandleType> {
    use ExternalSemaphoreHandleType::*;
    for handle_type in [OpaqueWin32Kmt, OpaqueWin32, D3D12Fence, OpaqueFd, SyncFd] {
        println!("Handle Type: {:?}", handle_type);
        if let Ok(properties) = physical_device
            .external_semaphore_properties(ExternalSemaphoreInfo::handle_type(handle_type))
        {
            println!("Properties: {:?}", properties);
            if properties
                .compatible_handle_types
                .intersects(&handle_type.into())
                && properties.exportable
            {
                return Some(handle_type);
            }
        }
    }
    None
}

#[cfg(target_pointer_width = "64")]
fn format_handle(raw_ptr: *const c_void) -> String {
    format!("{:016x}", raw_ptr as usize)
}

#[cfg(windows)]
pub fn print_handle(
    identifier: &str,
    semaphore: &Arc<Semaphore>,
    handle_type: ExternalSemaphoreHandleType,
) {
    // Should really properly format the handle, but my lazy ass just relies on the Debug trait instead
    println!(
        "Connection data: {{\"semaphore\", \"{}\", \"{:?}\", \"{}\"}}",
        identifier,
        handle_type,
        format_handle(semaphore.export_win32_handle(handle_type).unwrap())
    );
}
