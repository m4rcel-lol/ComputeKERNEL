//! ACPI table parsing driver.
//!
//! Locates and parses ACPI tables provided by the bootloader RSDP pointer.

use crate::serial_println;
use acpi::{AcpiHandler, AcpiTables, PhysicalMapping};
use core::ptr::NonNull;

/// A simple ACPI handler that uses the physical memory offset mapping.
///
/// Assumes that physical memory is identity-mapped at `physical_memory_offset`.
#[derive(Clone)]
pub struct KernelAcpiHandler {
    physical_memory_offset: u64,
}

impl KernelAcpiHandler {
    pub fn new(physical_memory_offset: u64) -> Self {
        KernelAcpiHandler { physical_memory_offset }
    }
}

impl AcpiHandler for KernelAcpiHandler {
    unsafe fn map_physical_region<T>(
        &self,
        physical_address: usize,
        size: usize,
    ) -> PhysicalMapping<Self, T> {
        let virt = (physical_address as u64 + self.physical_memory_offset) as *mut T;
        PhysicalMapping::new(
            physical_address,
            NonNull::new(virt).expect("null physical mapping"),
            size,
            size,
            self.clone(),
        )
    }

    fn unmap_physical_region<T>(_region: &PhysicalMapping<Self, T>) {
        // Nothing to unmap — we use the permanent offset mapping.
    }
}

/// Parse ACPI tables from the RSDP address provided by the bootloader.
///
/// Returns `None` if no RSDP address was provided or parsing fails.
pub fn init(
    rsdp_addr: Option<u64>,
    physical_memory_offset: u64,
) -> Option<AcpiTables<KernelAcpiHandler>> {
    let rsdp = rsdp_addr?;
    let handler = KernelAcpiHandler::new(physical_memory_offset);
    match unsafe { AcpiTables::from_rsdp(handler, rsdp as usize) } {
        Ok(tables) => {
            serial_println!("[ACPI] Tables parsed successfully");
            Some(tables)
        }
        Err(e) => {
            serial_println!("[ACPI] Failed to parse tables: {:?}", e);
            None
        }
    }
}
