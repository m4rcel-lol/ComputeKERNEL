//! Bluetooth Host Controller Interface (HCI) stub.

use crate::serial_println;

/// Stub Bluetooth HCI driver.
pub struct BluetoothHci;

impl BluetoothHci {
    /// Initialize the HCI layer.
    pub fn init() -> Result<Self, &'static str> {
        serial_println!("[BT] Bluetooth HCI stub initialized");
        Ok(BluetoothHci)
    }

    /// Stub: send an HCI command packet.
    pub fn send_command(&self, _opcode: u16, _params: &[u8]) -> Result<(), &'static str> {
        Err("Bluetooth HCI not yet implemented")
    }

    /// Stub: read an HCI event packet.
    pub fn read_event(&self, _buf: &mut [u8]) -> Result<usize, &'static str> {
        Err("Bluetooth HCI not yet implemented")
    }
}
