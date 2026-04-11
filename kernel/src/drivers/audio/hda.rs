//! Intel HDA audio controller stub.

use crate::serial_println;
use crate::drivers::pci::PciDevice;

pub const HDA_CLASS: u8 = 0x04;
pub const HDA_SUBCLASS: u8 = 0x03;

/// Stub HDA controller.
pub struct HdaController {
    _pci: PciDevice,
}

impl HdaController {
    /// Attempt to bind an HDA controller to a PCI device.
    pub fn probe(pci: PciDevice) -> Option<Self> {
        if pci.class == HDA_CLASS && pci.subclass == HDA_SUBCLASS {
            serial_println!(
                "[HDA] Found audio controller at {:02x}:{:02x}.{}",
                pci.bus, pci.device, pci.function
            );
            Some(HdaController { _pci: pci })
        } else {
            None
        }
    }

    /// Stub: play a PCM audio buffer.
    pub fn play_pcm(&self, _samples: &[i16]) -> Result<(), &'static str> {
        Err("HDA driver not yet implemented")
    }
}
