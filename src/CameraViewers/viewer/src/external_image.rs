use std::sync::Arc;
use vulkano::image::view::ImageView;
use vulkano::image::{Image, ImageMemory, ImageType};
use vulkano::memory::allocator::MemoryTypeFilter;
use vulkano::memory::{MemoryAllocateInfo, ResourceMemory};
use vulkano::{
    DeviceSize, Validated, VulkanError,
    device::Device,
    format::Format,
    image::{
        ImageCreateFlags, ImageUsage,
        sys::{ImageCreateInfo, RawImage},
    },
    memory::{
        DedicatedAllocation, DeviceMemory, ExternalMemoryHandleType, allocator::MemoryAllocator,
    },
};
use crate::platform::NativeManagedHandle;

#[derive(Debug)]
pub struct ExternalImage {
    inner: Arc<Image>,
    handle_type: ExternalMemoryHandleType,
}

pub type ExternalImageError = VulkanError;

impl From<&ExternalImage> for Arc<Image> {
    fn from(image: &ExternalImage) -> Self {
        image.inner.clone()
    }
}

impl ExternalImage {
    pub fn new<M>(
        device: Arc<Device>,
        memory_allocator: &M,
        dimensions: [u32; 2],
        handle_type: ExternalMemoryHandleType,
    ) -> Result<Self, ExternalImageError>
    where
        M: MemoryAllocator,
    {
        let raw_image = RawImage::new(
            device.clone(),
            ImageCreateInfo {
                flags: ImageCreateFlags::MUTABLE_FORMAT,
                image_type: ImageType::Dim2d,
                format: Format::R16G16B16A16_UNORM,
                extent: [dimensions[0], dimensions[1], 1],
                usage: ImageUsage::TRANSFER_SRC | ImageUsage::TRANSFER_DST | ImageUsage::SAMPLED,
                external_memory_handle_types: handle_type.into(),
                ..Default::default()
            },
        )
        .map_err(Validated::unwrap)?;

        let image_requirements = raw_image.memory_requirements()[0];

        let image_memory = DeviceMemory::allocate(
            device,
            MemoryAllocateInfo {
                allocation_size: image_requirements.layout.size(),
                memory_type_index: memory_allocator
                    .find_memory_type_index(
                        image_requirements.memory_type_bits,
                        MemoryTypeFilter::PREFER_DEVICE,
                    )
                    .unwrap(),
                dedicated_allocation: Some(DedicatedAllocation::Image(&raw_image)),
                export_handle_types: handle_type.into(),
                ..Default::default()
            },
        )
        .map_err(Validated::unwrap)?;

        // let allocation_size = image_memory.allocation_size();
        // let image_fd = image_memory
        //     .export_fd(ExternalMemoryHandleType::OpaqueFd)
        //     .unwrap();

        let image = Arc::new(
            raw_image
                .bind_memory([ResourceMemory::new_dedicated(image_memory)])
                .map_err(|(err, _, _)| err.unwrap())?,
        );

        Ok(Self {
            inner: image,
            handle_type,
        })
    }

    #[cfg(windows)]
    fn export_memory(
        memory: &DeviceMemory,
        handle_type: ExternalMemoryHandleType,
    ) -> Result<NativeManagedHandle, VulkanError> {
        use vulkano::VulkanObject;
        use std::mem::MaybeUninit;
        use ash::vk::MemoryGetWin32HandleInfoKHR;
        use windows::Win32::Foundation::HANDLE;
        use vulkano::device::DeviceOwned;

        // VUID-VkMemoryGetFdInfoKHR-handleType-parameter
        // handle_type.validate_device(memory.device())?; // Private function. Probably fine...


        // VUID-VkMemoryGetFdInfoKHR-handleType-00672

        if !matches!(
            handle_type,
            ExternalMemoryHandleType::OpaqueWin32
                | ExternalMemoryHandleType::OpaqueWin32Kmt
                | ExternalMemoryHandleType::D3D11Texture
                | ExternalMemoryHandleType::D3D11TextureKmt
                | ExternalMemoryHandleType::D3D12Resource
                | ExternalMemoryHandleType::D3D12Heap
        ) {
            panic!("Unsupported handle type: {:?}", handle_type);
        }

        // VUID-VkMemoryGetFdInfoKHR-handleType-00671

        // if !ash::vk::ExternalMemoryHandleTypeFlags::from(self.export_handle_types)
        //     .intersects(ash::vk::ExternalMemoryHandleTypeFlags::from(handle_type))
        // {
        //     return Err(DeviceMemoryError::HandleTypeNotSupported { handle_type });
        // }

        assert!(
            memory
                .device()
                .enabled_extensions()
                .khr_external_memory_win32
        );

        // let fd = unsafe { ... };
        // let file = unsafe { std::fs::File::from_raw_fd(fd) };
        // Ok(file)

        Ok(unsafe {
            let fns = memory.device().fns();
            let info = MemoryGetWin32HandleInfoKHR {
                memory: memory.handle(),
                handle_type: handle_type.into(),
                ..Default::default()
            };

            let mut output = MaybeUninit::uninit();
            (fns.khr_external_memory_win32.get_memory_win32_handle_khr)(
                memory.device().handle(),
                &info,
                output.as_mut_ptr(),
            )
            .result()
            .map_err(VulkanError::from)?;
            HANDLE(output.assume_init() as *mut std::ffi::c_void)
        })
    }

    #[cfg(unix)]
    fn export_memory(
        memory: &DeviceMemory,
        handle_type: ExternalMemoryHandleType,
    ) -> Result<NativeManagedHandle, VulkanError> {
        memory.export_fd(handle_type).map_err(Validated::unwrap)
    }

    fn get_device_memory(&self) -> &DeviceMemory {
        let allocation = match self.inner.memory() {
            ImageMemory::Normal(a) => &a[0],
            _ => unreachable!(),
        };

        allocation.device_memory()
    }

    pub fn export(&self) -> Result<NativeManagedHandle, VulkanError> {
        Self::export_memory(self.get_device_memory(), self.handle_type)
    }

    pub fn device_memory_allocation_size(&self) -> DeviceSize {
        self.get_device_memory().allocation_size()
    }

    pub fn format(&self) -> Format {
        self.inner.format()
    }
}

impl TryFrom<&ExternalImage> for Arc<ImageView> {
    type Error = VulkanError;
    fn try_from(value: &ExternalImage) -> Result<Self, Self::Error> {
        ImageView::new_default(value.inner.clone()).map_err(Validated::unwrap)
    }
}
