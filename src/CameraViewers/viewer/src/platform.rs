use std::io::IoSlice;
use std::os::fd::AsRawFd;
use std::os::unix::net::{SocketAncillary, UnixStream};
use std::path::Path;
use crate::external_image::ExternalImage;
use std::sync::Arc;
use vulkano::image::Image;
use vulkano::sync::semaphore::{ExternalSemaphoreHandleType, ExternalSemaphoreInfo, Semaphore};
#[cfg(unix)]
use vulkano::Validated;
use vulkano::{
    buffer::{BufferUsage, ExternalBufferInfo},
    device::physical::PhysicalDevice,
    memory::ExternalMemoryHandleType,
};
#[cfg(windows)]
use windows::Win32::Foundation::HANDLE as WinHandle;

#[cfg(windows)]
pub type NativeManagedHandle = WinHandle;
#[cfg(unix)]
pub type NativeManagedHandle = std::fs::File;

#[cfg(windows)]
type NativeRawHandle = std::os::windows::raw::HANDLE;
// #[cfg(unix)]
// type NativeRawHandle = std::os::fd::RawFd;

// Rust does not allow anonymous structs (yet), and so the current usage of this enum will have Rust
// complain about fields not being read
#[allow(dead_code)]
enum MemoryOwnerObject {
    Semaphore(Arc<Semaphore>),
    Image(Arc<Image>),
}

// Stores managed IPC handles.
// On unix: This stores Fd file handles, which need to be kept alive, hence this object
// On Windows: This stores native Win32 handles, which are managed by the OS and not us (I hope)
// #[derive(Default)]
pub struct MemoryExporter {
    #[cfg(unix)]
    handles: Vec<NativeManagedHandle>,
    #[cfg(unix)]
    owner_objects: Vec<MemoryOwnerObject>,
    #[cfg(unix)]
    pub(crate) channel: Option<OwnerChannel>,
}

impl MemoryExporter {
    #[cfg(unix)]
    pub fn is_valid(&self) -> bool {
        for handle in self.handles.iter() {
            if unsafe { libc::fcntl(handle.as_raw_fd(), libc::F_GETFD) } < 0 {
                return false;
            }
        }
        true
    }
    #[cfg(windows)]
    pub fn is_valid(&self) -> bool {
        true
    }

    pub fn from_channel(channel: OwnerChannel) -> MemoryExporter {
        MemoryExporter {
            handles: Default::default(),
            owner_objects: Default::default(),
            channel: Some(channel),
        }
    }
}

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
                .intersects(handle_type.into())
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
                .intersects(handle_type.into())
                && properties.external_memory_properties.exportable
            {
                return Some(handle_type);
            }
        }
    }
    None
}

#[cfg(all(windows, target_pointer_width = "64"))]
fn format_handle(handle: &NativeManagedHandle) -> String {
    let raw_ptr = handle.0 as NativeRawHandle;
    format!("{:016x}", raw_ptr as usize)
}

#[cfg(unix)]
fn format_handle(handle: &NativeManagedHandle) -> String {
    use std::os::fd::AsRawFd;
    format!("{}", handle.as_raw_fd())
}

fn format_semaphore_handle(identifier: &str, handle: &NativeManagedHandle) -> String {
    const HANDLE_TYPE: &'static str = if cfg!(windows) {
        "OpaqueWin32"
    } else {
        "OpaqueFd"
    };
    format!(
        "Connection data: {{\"semaphore\", \"{}\", handle type: \"{}\", \"{}\"}}",
        identifier,
        HANDLE_TYPE,
        format_handle(handle)
    )
}

fn format_memory_handle(identifier: &str, image: &ExternalImage, handle: &NativeManagedHandle) -> String {
    const HANDLE_TYPE: &'static str = if cfg!(windows) {
        "OpaqueWin32"
    } else {
        "OpaqueFd"
    };
    format!(
        "Connection data: {{\"image\", \"{}\", handle type: \"{}\", \"{}\", size: \"{}\", format: \"{:?}\" }}",
        identifier,
        HANDLE_TYPE,
        format_handle(handle),
        image.device_memory_allocation_size(),
        image.format()
    )
}

#[cfg(windows)]
unsafe fn find_parent_process_id() -> Option<u32> {
    use std::mem::size_of;

    use windows::Win32::{
        Foundation::CloseHandle,
        System::{
            Diagnostics::ToolHelp::{
                CreateToolhelp32Snapshot, PROCESSENTRY32, Process32First, Process32Next,
                TH32CS_SNAPPROCESS,
            },
            Threading::GetCurrentProcessId,
        },
    };

    unsafe {
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
    }

    None
}

#[cfg(windows)]
unsafe fn create_owner_process_accessible_memory_handle(memory_handle: &WinHandle) -> WinHandle {
    use windows::Win32::{
        Foundation::{CloseHandle, DUPLICATE_SAME_ACCESS, DuplicateHandle},
        System::Threading::{GetCurrentProcess, OpenProcess, PROCESS_ALL_ACCESS},
    };

    let mut new_handle = WinHandle::default();
    unsafe {
        let current_process = GetCurrentProcess();
        let parent_process_id = find_parent_process_id().unwrap();
        let parent_process = OpenProcess(PROCESS_ALL_ACCESS, false, parent_process_id).unwrap();
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
    }
    new_handle
}

impl MemoryExporter {
    #[cfg(windows)]
    pub fn export_semaphore_to_owner_process(
        &mut self,
        identifier: &str,
        semaphore: &Arc<Semaphore>,
        handle_type: ExternalSemaphoreHandleType,
    ) {
        if handle_type != ExternalSemaphoreHandleType::OpaqueWin32 {
            unimplemented!(
                "Windows export function has only been implemented for Opaque Win 32 NT handles"
            );
        }

        let exported_handle = WinHandle(semaphore.export_win32_handle(handle_type).unwrap() as *mut std::ffi::c_void);
        unsafe {
            let new_handle = create_owner_process_accessible_memory_handle(&exported_handle);
            format_semaphore_handle(identifier, &new_handle);
        };
    }

    #[cfg(unix)]
    pub fn export_semaphore_to_owner_process(
        &mut self,
        identifier: &str,
        semaphore: &Arc<Semaphore>,
        handle_type: ExternalSemaphoreHandleType,
    ) {
        if handle_type != ExternalSemaphoreHandleType::OpaqueFd {
            unimplemented!("Unix export function has only been implemented for Opaque FD handles");
        }

        self.owner_objects
            .push(MemoryOwnerObject::Semaphore(semaphore.clone()));

        unsafe {
            let file = semaphore
                .export_fd(handle_type)
                .map_err(Validated::unwrap)
                .unwrap();
            self.channel.as_ref().unwrap().send(&format_semaphore_handle(identifier, &file), &file);
            self.handles.push(file);
        }

        // TODO: In owner-process, use pidfd_getfd to duplicate (steal) the handle
    }

    #[cfg(windows)]
    pub fn export_memory_to_owner_process(
        &mut self,
        identifier: &str,
        image: &ExternalImage,
        handle_type: ExternalMemoryHandleType,
    ) {
        if handle_type != ExternalMemoryHandleType::OpaqueWin32 {
            unimplemented!(
                "Windows export function has only been implemented for Opaque Win 32 NT handles"
            );
        }

        let exported_handle = image.export().unwrap();
        unsafe {
            let new_handle = create_owner_process_accessible_memory_handle(&exported_handle);
            format_memory_handle(identifier, image, &new_handle);
        };
    }

    #[cfg(unix)]
    pub fn export_memory_to_owner_process(
        &mut self,
        identifier: &str,
        image: &ExternalImage,
        handle_type: ExternalMemoryHandleType,
    ) {
        if handle_type != ExternalMemoryHandleType::OpaqueFd {
            unimplemented!("Unix export function has only been implemented for Opaque FD handles");
        }

        self.owner_objects
            .push(MemoryOwnerObject::Image(image.into()));

        let file = image.export().unwrap();
        self.channel.as_ref().unwrap().send(&format_memory_handle(identifier, image, &file), &file);
        self.handles.push(file);
    }
}

#[cfg(windows)]
pub struct OwnerChannel;
#[cfg(unix)]
pub struct OwnerChannel {
    stream: UnixStream,
}

impl OwnerChannel {
    pub fn new(socket_path: &Path) -> std::io::Result<OwnerChannel> {
        let stream = UnixStream::connect(socket_path)?;
        Ok(OwnerChannel { stream })
    }

    #[cfg(unix)]
    pub fn send(&self, msg: &str, handle: &NativeManagedHandle) {
        let mut ancillary_buffer = [0; 128];
        let mut ancillary = SocketAncillary::new(&mut ancillary_buffer[..]);
        ancillary.add_fds(&[handle.as_raw_fd()][..]);
        let io_slice_path = IoSlice::new(msg.as_bytes());
        self.stream.send_vectored_with_ancillary(&[io_slice_path][..], &mut ancillary).unwrap();
    }
}
