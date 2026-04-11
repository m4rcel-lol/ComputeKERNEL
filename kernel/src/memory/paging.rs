//! Virtual memory paging utilities.

use x86_64::{
    registers::control::Cr3,
    structures::paging::{OffsetPageTable, PageTable},
    VirtAddr,
};

/// Initialize an `OffsetPageTable` using the physical memory offset provided
/// by the bootloader.
///
/// # Safety
/// - `physical_memory_offset` must be the correct offset to translate physical
///   addresses to virtual addresses (as provided by bootloader_api).
/// - Must be called only once.
pub unsafe fn init(physical_memory_offset: VirtAddr) -> OffsetPageTable<'static> {
    let level_4_table = active_level_4_table(physical_memory_offset);
    OffsetPageTable::new(level_4_table, physical_memory_offset)
}

/// Return a mutable reference to the active level-4 page table.
///
/// # Safety
/// - `physical_memory_offset` must map the full physical address space.
/// - The caller must ensure this is only called once and not aliased.
unsafe fn active_level_4_table(physical_memory_offset: VirtAddr) -> &'static mut PageTable {
    let (level_4_frame, _) = Cr3::read();
    let phys = level_4_frame.start_address();
    let virt = physical_memory_offset + phys.as_u64();
    let page_table_ptr: *mut PageTable = virt.as_mut_ptr();
    &mut *page_table_ptr
}

/// Translate a virtual address to its physical counterpart using the offset
/// mapping (does not walk the page table).
pub fn phys_from_virt(virt: VirtAddr, physical_memory_offset: VirtAddr) -> Option<u64> {
    let phys = virt.as_u64().checked_sub(physical_memory_offset.as_u64())?;
    Some(phys)
}
