//! xHCI USB 3.x host controller driver stub.

use crate::serial_println;
use crate::drivers::pci::PciDevice;

pub const XHCI_CLASS: u8 = 0x0C;
pub const XHCI_SUBCLASS: u8 = 0x03;
pub const XHCI_PROG_IF: u8 = 0x30;

/// Stub xHCI controller.
pub struct XhciController {
    _pci: PciDevice,
}

impl XhciController {
    /// Attempt to bind an xHCI controller to a PCI device.
    pub fn probe(pci: PciDevice) -> Option<Self> {
        if pci.class == XHCI_CLASS && pci.subclass == XHCI_SUBCLASS && pci.prog_if == XHCI_PROG_IF
        {
            serial_println!(
                "[xHCI] Found USB 3 controller at {:02x}:{:02x}.{}",
                pci.bus, pci.device, pci.function
            );
            Some(XhciController { _pci: pci })
        } else {
            None
        }
    }

    /// Stub: enumerate connected USB devices.
    pub fn enumerate_devices(&self) -> Result<(), &'static str> {
        Err("xHCI driver not yet implemented")
    }
}
