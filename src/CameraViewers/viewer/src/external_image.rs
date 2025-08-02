use crate::platform::NativeManagedHandle;
use std::sync::Arc;
use vulkano::command_buffer::allocator::CommandBufferAllocator;
use vulkano::command_buffer::{
    AutoCommandBufferBuilder, ClearColorImageInfo, CommandBufferUsage, PrimaryCommandBufferAbstract,
};
use vulkano::device::Queue;
use vulkano::format::ClearColorValue;
use vulkano::image::{Image, ImageLayout, ImageMemory, ImageType};
use vulkano::memory::allocator::MemoryTypeFilter;
use vulkano::memory::{MemoryAllocateInfo, ResourceMemory};
use vulkano::sync::GpuFuture;
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

impl From<ExternalImage> for Arc<Image> {
    fn from(image: ExternalImage) -> Self {
        image.inner
    }
}

impl ExternalImage {
    pub fn new<M, C>(
        device: Arc<Device>,
        memory_allocator: &M,
        command_buffer_allocator: &Arc<C>,
        queue: &Arc<Queue>,
        dimensions: [u32; 2],
        handle_type: ExternalMemoryHandleType,
    ) -> Result<Self, ExternalImageError>
    where
        M: MemoryAllocator,
        C: CommandBufferAllocator,
    {
        let raw_image = RawImage::new(
            device.clone(),
            ImageCreateInfo {
                flags: ImageCreateFlags::MUTABLE_FORMAT,
                image_type: ImageType::Dim2d,
                format: Format::R16G16B16A16_UNORM,
                extent: [dimensions[0], dimensions[1], 1],
                // usage: ImageUsage::TRANSFER_DST | ImageUsage::SAMPLED | ImageUsage::COLOR_ATTACHMENT,
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

        // External images need to have its memory initialised immediately as they may be
        // externally written.
        // let mut builder = AutoCommandBufferBuilder::primary(
        //     command_buffer_allocator.clone(),
        //     queue.queue_family_index(),
        //     CommandBufferUsage::OneTimeSubmit,
        // )
        // .map_err(Validated::unwrap)
        // .unwrap();
        //
        // builder
        //     .clear_color_image(ClearColorImageInfo {
        //         clear_value: ClearColorValue::Float([1.0, 0.0, 1.0, 1.0]),
        //         image_layout: ImageLayout::General,
        //         ..ClearColorImageInfo::image(image.clone())
        //     })
        //     .unwrap();
        // let command_buffer = builder.build().unwrap();
        // let future = command_buffer.execute(queue.clone()).unwrap();
        // future.flush().unwrap();

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
        use ash::vk::MemoryGetWin32HandleInfoKHR;
        use std::mem::MaybeUninit;
        use vulkano::VulkanObject;
        use vulkano::device::DeviceOwned;
        use windows::Win32::Foundation::HANDLE;

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
        assert_eq!(handle_type, ExternalMemoryHandleType::OpaqueFd);
        memory
            .export_fd(ExternalMemoryHandleType::OpaqueFd)
            .map_err(Validated::unwrap)
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

    pub fn dimensions(&self) -> (u32, u32) {
        let extent = self.inner.extent();
        (extent[0], extent[1])
    }
}
