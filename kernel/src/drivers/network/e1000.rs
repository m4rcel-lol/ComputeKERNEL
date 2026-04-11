//! Intel e1000 Gigabit Ethernet driver stub.

use crate::serial_println;
use crate::drivers::pci::PciDevice;

pub const E1000_VENDOR: u16 = 0x8086;
pub const E1000_DEVICE: u16 = 0x100E;

/// Stub e1000 driver.
pub struct E1000Driver {
    _pci: PciDevice,
}

impl E1000Driver {
    /// Attempt to bind an e1000 driver to a PCI device.
    pub fn probe(pci: PciDevice) -> Option<Self> {
        if pci.vendor_id == E1000_VENDOR && pci.device_id == E1000_DEVICE {
            serial_println!(
                "[e1000] Found NIC at {:02x}:{:02x}.{}",
                pci.bus, pci.device, pci.function
            );
            Some(E1000Driver { _pci: pci })
        } else {
            None
        }
    }

    /// Stub: transmit an Ethernet frame.
    pub fn send(&self, _frame: &[u8]) -> Result<(), &'static str> {
        Err("e1000 driver not yet implemented")
    }

    /// Stub: receive an Ethernet frame.
    pub fn recv(&self, _buf: &mut [u8]) -> Result<usize, &'static str> {
        Err("e1000 driver not yet implemented")
    }
}
