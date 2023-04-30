use std::{ffi::c_void, sync::Arc};
use vulkano::{
    buffer::{BufferUsage, ExternalBufferInfo},
    device::physical::PhysicalDevice,
    memory::ExternalMemoryHandleType,
    sync::{ExternalSemaphoreHandleType, ExternalSemaphoreInfo, Semaphore},
};

use crate::external_image::ExternalImage;

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

pub fn get_external_memory_type(
    physical_device: &PhysicalDevice,
    usage: BufferUsage,
) -> Option<ExternalMemoryHandleType> {
    use ExternalMemoryHandleType::*;
    // The preferred order is (Windows non-owning opaque handles, Windows owning opaque handles, Linux file descriptor handles)
    // TODO: If this is slow, consider switching to VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT or VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_MAPPED_FOREIGN_MEMORY_BIT_EXT
    // which should be directly mapped on the device (hopefully skipping the expensive operation of intermediate CPU copying)
    for handle_type in [
        D3D11TextureKmt,
        D3D11Texture,
        D3D12Resource,
        D3D12Heap,
        OpaqueWin32Kmt,
        OpaqueWin32,
        OpaqueFd,
        DmaBuf,
    ] {
        println!("Handle Type: {:?}", handle_type);
        let mut info = ExternalBufferInfo::handle_type(handle_type);
        info.usage = usage;
        if let Ok(properties) = physical_device.external_buffer_properties(info) {
            println!("Properties: {:?}", properties);
            if properties
                .external_memory_properties
                .compatible_handle_types
                .intersects(&handle_type.into())
                && properties.external_memory_properties.exportable
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
pub fn print_semaphore_handle(
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

#[cfg(windows)]
pub fn print_memory_handle(
    identifier: &str,
    image: &Arc<ExternalImage>,
    handle_type: ExternalMemoryHandleType,
) {
    // Should really properly format the handle, but my lazy ass just relies on the Debug trait instead

    println!(
        "Connection data: {{\"image\", \"{}\", \"{:?}\", \"{}\", size: \"{}\", format: \"{:?}\" }}",
        identifier,
        handle_type,
        format_handle(image.export().unwrap()),
        image.as_ref().device_memory_allocation_size(),
        image.format()
    );
}

// pub fn export_memory_handle(image: &StorageImage) -> Result<_, DeviceMemoryError> {
//     let allocation = match image.inner.memory() {
//         ImageMemory::Normal(a) => &a[0],
//         _ => unreachable!(),
//     };

//     allocation
//         .device_memory()
//         .export_fd(ExternalMemoryHandleType::OpaqueFd)
// }
