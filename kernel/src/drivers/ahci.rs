//! AHCI / SATA storage driver stub.
//!
//! A future implementation will enumerate AHCI ports via the HBA MMIO region.

use crate::serial_println;
use crate::drivers::pci::PciDevice;

/// AHCI controller PCI class/subclass/prog-if codes.
pub const AHCI_CLASS: u8 = 0x01;
pub const AHCI_SUBCLASS: u8 = 0x06;
pub const AHCI_PROG_IF: u8 = 0x01;

/// Stub AHCI HBA.
pub struct AhciController {
    pci: PciDevice,
}

impl AhciController {
    /// Attempt to bind an AHCI controller to a PCI device.
    pub fn probe(pci: PciDevice) -> Option<Self> {
        if pci.class == AHCI_CLASS && pci.subclass == AHCI_SUBCLASS && pci.prog_if == AHCI_PROG_IF
        {
            serial_println!(
                "[AHCI] Found controller at {:02x}:{:02x}.{}",
                pci.bus, pci.device, pci.function
            );
            Some(AhciController { pci })
        } else {
            None
        }
    }

    /// Stub: read sectors from a SATA disk.
    pub fn read_sectors(
        &self,
        _port: u8,
        _lba: u64,
        _count: u16,
        _buf: &mut [u8],
    ) -> Result<(), &'static str> {
        Err("AHCI driver not yet implemented")
    }

    /// Stub: write sectors to a SATA disk.
    pub fn write_sectors(
        &self,
        _port: u8,
        _lba: u64,
        _count: u16,
        _buf: &[u8],
    ) -> Result<(), &'static str> {
        Err("AHCI driver not yet implemented")
    }
}
