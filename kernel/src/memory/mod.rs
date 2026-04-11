//! Memory management subsystem.
//!
//! Provides:
//! - Physical frame allocator backed by the bootloader memory map.
//! - Virtual memory mapper using the physical memory offset mapping.
//! - Kernel heap initialization via `linked_list_allocator`.

use bootloader_api::BootInfo;
use linked_list_allocator::LockedHeap;
use x86_64::VirtAddr;

pub mod frame_allocator;
pub mod heap;
pub mod paging;

// ── Global Heap Allocator ─────────────────────────────────────────────────

#[global_allocator]
static ALLOCATOR: LockedHeap = LockedHeap::empty();

/// Virtual address where the kernel heap starts.
pub const HEAP_START: usize = 0x_4444_4444_0000;
/// Size of the kernel heap (2 MiB).
pub const HEAP_SIZE: usize = 2 * 1024 * 1024;

/// Initialize all memory subsystems.
///
/// Must be called after GDT and IDT are set up, and before any heap
/// allocations are made.
pub fn init(boot_info: &'static mut BootInfo) {
    let phys_mem_offset = {
        use bootloader_api::info::Optional;
        match boot_info.physical_memory_offset {
            Optional::Some(offset) => VirtAddr::new(offset),
            Optional::None => panic!("bootloader did not provide physical memory offset"),
        }
    };

    // Safety: called once, boot_info is valid for the kernel's lifetime.
    let mut mapper = unsafe { paging::init(phys_mem_offset) };
    let mut frame_allocator =
        unsafe { frame_allocator::BootInfoFrameAllocator::init(&boot_info.memory_regions) };

    heap::init_heap(&mut mapper, &mut frame_allocator)
        .expect("kernel heap initialization failed");

    unsafe {
        ALLOCATOR
            .lock()
            .init(HEAP_START as *mut u8, HEAP_SIZE);
    }
}
