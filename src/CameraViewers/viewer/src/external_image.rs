use std::{mem::MaybeUninit, sync::Arc};

use ash::vk::MemoryGetWin32HandleInfoKHR;
use vulkano::{
    device::DeviceOwned,
    format::Format,
    image::{
        sys::{Image, ImageCreateInfo, ImageMemory, RawImage},
        ImageCreateFlags, ImageDimensions, ImageError, ImageFormatInfo, ImageUsage,
    },
    memory::{
        allocator::{AllocationCreationError, MemoryAllocator, MemoryUsage},
        DedicatedAllocation, DeviceMemory, DeviceMemoryError, ExternalMemoryHandleType,
    },
    sync::Sharing,
    DeviceSize,
    // OomError,
    VulkanError,
};

#[derive(Debug)]
pub struct ExternalImage {
    inner: Arc<Image>,
    handle_type: ExternalMemoryHandleType,
}

#[derive(Debug)]
pub enum ExternalImageError {
    ImageError(ImageError),
    // OomError(OomError),
    AllocationCreationError(AllocationCreationError),
    VulkanError(VulkanError),
    // Unimplemented,
}

impl From<ImageError> for ExternalImageError {
    fn from(error: ImageError) -> Self {
        ExternalImageError::ImageError(error)
    }
}

impl From<AllocationCreationError> for ExternalImageError {
    fn from(error: AllocationCreationError) -> Self {
        ExternalImageError::AllocationCreationError(error)
    }
}

impl From<VulkanError> for ExternalImageError {
    fn from(error: VulkanError) -> Self {
        ExternalImageError::VulkanError(error)
    }
}

impl ExternalImage {
    // pub fn new(
    //     device: Arc<Device>,
    //     create_info: ExternalImageCreateInfo,
    // ) -> Result<ExternalImage, ExternalImageError> {
    //     let image = UnsafeImage::new(device, create_info.into())?;

    //     let mem_reqs = image.memory_requirements();
    //     let memory = MemoryPool::alloc_from_requirements(
    //         &Device::standard_pool(&device),
    //         &mem_reqs,
    //         AllocLayout::Optimal,
    //         MappingRequirement::DoNotMap,
    //         Some(DedicatedAllocation::Image(&image)),
    //         |t| {
    //             if t.is_device_local() {
    //                 AllocFromRequirementsFilter::Preferred
    //             } else {
    //                 AllocFromRequirementsFilter::Allowed
    //             }
    //         },
    //     )?;
    //     // debug_assert!((memory.offset() % mem_reqs.alignment) == 0);
    //     unsafe {
    //         image.bind_memory(memory.memory(), memory.offset())?;
    //     }

    //     Ok(ExternalImage { image: image })
    // }

    // Copied from vulkano/image/storage.rs
    pub fn new(
        allocator: &(impl MemoryAllocator + ?Sized),
        dimensions: ImageDimensions,
        format: Format,
        usage: ImageUsage,
        flags: ImageCreateFlags,
        queue_family_indices: impl IntoIterator<Item = u32>,
        handle_type: ExternalMemoryHandleType,
    ) -> Result<Arc<ExternalImage>, ExternalImageError> {
        assert_eq!(
            queue_family_indices.into_iter().count(),
            1,
            "This function currently only supports one queue family."
        );
        // let queue_family_indices = queue_family_indices.into_iter().collect();
        assert!(!flags.disjoint); // TODO: adjust the code below to make this safe

        let external_memory_properties = allocator
            .device()
            .physical_device()
            .image_format_properties(ImageFormatInfo {
                flags,
                format: Some(format),
                image_type: dimensions.image_type(),
                usage,
                external_memory_handle_type: Some(handle_type),
                ..Default::default()
            })
            .unwrap()
            .unwrap()
            .external_memory_properties;
        // VUID-VkExportMemoryAllocateInfo-handleTypes-00656
        assert!(external_memory_properties.exportable);

        // VUID-VkMemoryAllocateInfo-pNext-00639
        // Guaranteed because we always create a dedicated allocation

        // let external_memory_handle_types = handle_type.into();
        let raw_image = RawImage::new(
            allocator.device().clone(),
            ImageCreateInfo {
                flags,
                dimensions,
                format: Some(format),
                usage,
                // sharing: if queue_family_indices.len() >= 2 {
                //     Sharing::Concurrent(queue_family_indices)
                // } else {
                //     Sharing::Exclusive
                // },
                sharing: Sharing::Exclusive,
                external_memory_handle_types: handle_type.into(),
                ..Default::default()
            },
        )?;
        let requirements = raw_image.memory_requirements()[0];
        let memory_type_index = allocator
            .find_memory_type_index(requirements.memory_type_bits, MemoryUsage::GpuOnly.into())
            .expect("failed to find a suitable memory type");

        match unsafe {
            allocator.allocate_dedicated_unchecked(
                memory_type_index,
                requirements.size,
                Some(DedicatedAllocation::Image(&raw_image)),
                handle_type.into(),
            )
        } {
            Ok(alloc) => {
                debug_assert!(alloc.offset() % requirements.alignment == 0);
                debug_assert!(alloc.size() == requirements.size);
                let inner = Arc::new(unsafe {
                    raw_image
                        .bind_memory_unchecked([alloc])
                        .map_err(|(err, _, _)| err)?
                });

                Ok(Arc::new(ExternalImage { inner, handle_type }))
            }
            Err(err) => Err(err.into()),
        }
    }

    #[cfg(windows)]
    fn export_memory(
        memory: &DeviceMemory,
        handle_type: ExternalMemoryHandleType,
    ) -> Result<*mut std::ffi::c_void, DeviceMemoryError> {
        // VUID-VkMemoryGetFdInfoKHR-handleType-parameter
        // handle_type.validate_device(memory.device())?; // Private function. Probably fine...

        use vulkano::VulkanObject;

        if cfg!(not(windows)) {
            unreachable!("You should not be here");
        }

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
            return Err(DeviceMemoryError::HandleTypeNotSupported { handle_type });
        }

        // VUID-VkMemoryGetFdInfoKHR-handleType-00671

        // if !ash::vk::ExternalMemoryHandleTypeFlags::from(self.export_handle_types)
        //     .intersects(ash::vk::ExternalMemoryHandleTypeFlags::from(handle_type))
        // {
        //     return Err(DeviceMemoryError::HandleTypeNotSupported { handle_type });
        // }

        debug_assert!(
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
            output.assume_init()
        })
    }

    #[cfg(unix)]
    fn export_memory(
        memory: &DeviceMemory,
        handle_type: ExternalMemoryHandleType,
    ) -> Result<std::fs::File, DeviceMemoryError> {
        memory.export_fd(handle_type)
    }

    fn get_device_memory(&self) -> &DeviceMemory {
        let allocation = match self.inner.memory() {
            ImageMemory::Normal(a) => &a[0],
            _ => unreachable!(),
        };

        allocation.device_memory()
    }

    pub fn export(&self) -> Result<*mut std::ffi::c_void, DeviceMemoryError> {
        Self::export_memory(self.get_device_memory(), self.handle_type)
    }

    pub fn device_memory_allocation_size(&self) -> DeviceSize {
        self.get_device_memory().allocation_size()
    }

    pub fn format(&self) -> Format {
        self.inner.format().unwrap()
    }
}
