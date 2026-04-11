//! NVMe storage driver stub.
//!
//! A future implementation will use MMIO queues discovered via PCI BAR0.

use crate::serial_println;
use crate::drivers::pci::PciDevice;

/// NVMe controller PCI class/subclass codes.
pub const NVME_CLASS: u8 = 0x01;
pub const NVME_SUBCLASS: u8 = 0x08;

/// Stub NVMe controller.
pub struct NvmeController {
    pci: PciDevice,
}

impl NvmeController {
    /// Attempt to bind an NVMe controller to a PCI device.
    pub fn probe(pci: PciDevice) -> Option<Self> {
        if pci.class == NVME_CLASS && pci.subclass == NVME_SUBCLASS {
            serial_println!(
                "[NVMe] Found controller at {:02x}:{:02x}.{}",
                pci.bus, pci.device, pci.function
            );
            Some(NvmeController { pci })
        } else {
            None
        }
    }

    /// Stub: submit a read request.
    pub fn read_blocks(&self, _lba: u64, _count: u32, _buf: &mut [u8]) -> Result<(), &'static str> {
        Err("NVMe driver not yet implemented")
    }

    /// Stub: submit a write request.
    pub fn write_blocks(&self, _lba: u64, _count: u32, _buf: &[u8]) -> Result<(), &'static str> {
        Err("NVMe driver not yet implemented")
    }
}
