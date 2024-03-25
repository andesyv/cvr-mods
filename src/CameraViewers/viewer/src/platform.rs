use std::{ffi::c_void, sync::Arc};
use vulkano::{
    buffer::{BufferUsage, ExternalBufferInfo},
    device::physical::PhysicalDevice,
    memory::ExternalMemoryHandleType,
    sync::{ExternalSemaphoreHandleType, ExternalSemaphoreInfo, Semaphore},
};

use crate::external_image::ExternalImage;
use windows::Win32::Foundation::HANDLE;

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
    for handle_type in [OpaqueWin32, OpaqueWin32Kmt, D3D12Fence, OpaqueFd, SyncFd] {
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
    // The preferred order is (Windows owning opaque handles, Windows non-owning opaque handles, Linux file descriptor handles)
    // TODO: If this is slow, consider switching to VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT or VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_MAPPED_FOREIGN_MEMORY_BIT_EXT
    // which should be directly mapped on the device (hopefully skipping the expensive operation of intermediate CPU copying)
    for handle_type in [
        OpaqueWin32,
        D3D11TextureKmt,
        D3D11Texture,
        D3D12Resource,
        D3D12Heap,
        OpaqueWin32Kmt,
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
fn print_semaphore_handle(identifier: &str, handle: &HANDLE) {
    println!(
        "Connection data: {{\"semaphore\", \"{}\", \"{}\"}}",
        identifier,
        format_handle(handle.0 as std::os::windows::raw::HANDLE)
    );
}

#[cfg(windows)]
fn print_memory_handle(
    identifier: &str,
    handle: &HANDLE,
    image: &Arc<ExternalImage>,
) {
    println!(
        "Connection data: {{\"image\", \"{}\", \"{}\", size: \"{}\", format: \"{:?}\" }}",
        identifier,
        format_handle(handle.0 as std::os::windows::raw::HANDLE),
        image.as_ref().device_memory_allocation_size(),
        image.format()
    );
}

// #[cfg(windows)]
// pub fn print_memory_handle(
//     identifier: &str,
//     image: &Arc<ExternalImage>,
//     handle_type: ExternalMemoryHandleType,
// ) {
//     // Should really properly format the handle, but my lazy ass just relies on the Debug trait instead

//     println!(
//         "Connection data: {{\"image\", \"{}\", \"{:?}\", \"{}\", size: \"{}\", format: \"{:?}\" }}",
//         identifier,
//         handle_type,
//         format_handle(image.export().unwrap()),
//         image.as_ref().device_memory_allocation_size(),
//         image.format()
//     );
// }

// pub fn export_memory_handle(image: &StorageImage) -> Result<_, DeviceMemoryError> {
//     let allocation = match image.inner.memory() {
//         ImageMemory::Normal(a) => &a[0],
//         _ => unreachable!(),
//     };

//     allocation
//         .device_memory()
//         .export_fd(ExternalMemoryHandleType::OpaqueFd)
// }

#[cfg(windows)]
unsafe fn find_parent_process_id() -> Option<u32> {
    use std::mem::size_of;

    use windows::Win32::{
        Foundation::CloseHandle,
        System::{
            Diagnostics::ToolHelp::{
                CreateToolhelp32Snapshot, Process32First, Process32Next, PROCESSENTRY32,
                TH32CS_SNAPPROCESS,
            },
            Threading::GetCurrentProcessId,
        },
    };

    let current_process_id = GetCurrentProcessId();
    let snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0).ok()?;
    let mut process_entry = PROCESSENTRY32::default();
    process_entry.dwSize = size_of::<PROCESSENTRY32>().try_into().unwrap();
    Process32First(snapshot, &mut process_entry).unwrap();

    loop {
        if process_entry.th32ProcessID == current_process_id {
            CloseHandle(snapshot).unwrap();
            return Some(process_entry.th32ParentProcessID);
        }

        if Process32Next(snapshot, &mut process_entry).is_err() {
            break;
        }
    }

    CloseHandle(snapshot).unwrap();

    None
}

#[cfg(windows)]
unsafe fn create_owner_process_accessible_memory_handle(memory_handle: &HANDLE) -> HANDLE {
    use windows::Win32::{
        Foundation::{CloseHandle, DuplicateHandle, DUPLICATE_SAME_ACCESS},
        System::Threading::{GetCurrentProcess, OpenProcess, PROCESS_ALL_ACCESS},
    };

    let current_process = GetCurrentProcess();
    let parent_process_id = find_parent_process_id().unwrap();
    let parent_process = OpenProcess(PROCESS_ALL_ACCESS, false, parent_process_id).unwrap();
    let mut new_handle = HANDLE::default();
    DuplicateHandle(
        current_process,
        *memory_handle,
        parent_process,
        &mut new_handle,
        0,
        false,
        DUPLICATE_SAME_ACCESS,
    )
    .unwrap();
    CloseHandle(parent_process).unwrap();
    new_handle
}

#[cfg(windows)]
pub fn export_semaphore_to_owner_process(
    identifier: &str,
    semaphore: &Arc<Semaphore>,
    handle_type: ExternalSemaphoreHandleType,
) {
    if handle_type != ExternalSemaphoreHandleType::OpaqueWin32 {
        panic!("Windows export function has only been implemented for Opaque Win 32 NT handles");
    }

    let exported_handle = HANDLE(semaphore.export_win32_handle(handle_type).unwrap() as isize);
    unsafe {
        let new_handle = create_owner_process_accessible_memory_handle(&exported_handle);
        print_semaphore_handle(identifier, &new_handle);
    };
}

#[cfg(windows)]
pub fn export_memory_to_owner_process(
    identifier: &str,
    image: &Arc<ExternalImage>,
    handle_type: ExternalMemoryHandleType,
) {
    if handle_type != ExternalMemoryHandleType::OpaqueWin32 {
        panic!("Windows export function has only been implemented for Opaque Win 32 NT handles");
    }

    let exported_handle = HANDLE(image.export().unwrap() as isize);
    unsafe {
        let new_handle = create_owner_process_accessible_memory_handle(&exported_handle);
        print_memory_handle(identifier, &new_handle, image);
    };
}
