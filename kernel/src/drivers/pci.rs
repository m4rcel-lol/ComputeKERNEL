//! PCI bus enumeration and device configuration.
//!
//! Uses port-mapped I/O (PIO) to scan all 256 buses × 32 devices × 8 functions.

use crate::serial_println;
use spin::Mutex;
use x86_64::instructions::port::Port;

/// PCI configuration space I/O ports.
const CONFIG_ADDRESS: u16 = 0xCF8;
const CONFIG_DATA: u16 = 0xCFC;

static PCI_LOCK: Mutex<()> = Mutex::new(());

/// A discovered PCI device.
#[derive(Debug, Clone)]
pub struct PciDevice {
    pub bus: u8,
    pub device: u8,
    pub function: u8,
    pub vendor_id: u16,
    pub device_id: u16,
    pub class: u8,
    pub subclass: u8,
    pub prog_if: u8,
    pub revision: u8,
    pub header_type: u8,
}

/// Read a 32-bit dword from PCI configuration space.
///
/// # Safety
/// Accesses I/O ports — must be called from kernel context.
pub fn config_read32(bus: u8, device: u8, function: u8, offset: u8) -> u32 {
    let address: u32 = (1 << 31)
        | ((bus as u32) << 16)
        | ((device as u32) << 11)
        | ((function as u32) << 8)
        | ((offset as u32) & 0xFC);

    let _lock = PCI_LOCK.lock();
    unsafe {
        let mut addr_port: Port<u32> = Port::new(CONFIG_ADDRESS);
        let mut data_port: Port<u32> = Port::new(CONFIG_DATA);
        addr_port.write(address);
        data_port.read()
    }
}

/// Write a 32-bit dword to PCI configuration space.
///
/// # Safety
/// Accesses I/O ports — must be called from kernel context.
pub fn config_write32(bus: u8, device: u8, function: u8, offset: u8, value: u32) {
    let address: u32 = (1 << 31)
        | ((bus as u32) << 16)
        | ((device as u32) << 11)
        | ((function as u32) << 8)
        | ((offset as u32) & 0xFC);

    let _lock = PCI_LOCK.lock();
    unsafe {
        let mut addr_port: Port<u32> = Port::new(CONFIG_ADDRESS);
        let mut data_port: Port<u32> = Port::new(CONFIG_DATA);
        addr_port.write(address);
        data_port.write(value);
    }
}

/// Read a 16-bit word from PCI configuration space.
pub fn config_read16(bus: u8, device: u8, function: u8, offset: u8) -> u16 {
    let dword = config_read32(bus, device, function, offset & !2);
    let shift = (offset & 2) * 8;
    (dword >> shift) as u16
}

/// Read a PCI BAR (Base Address Register).
pub fn read_bar(dev: &PciDevice, bar_index: u8) -> u32 {
    config_read32(dev.bus, dev.device, dev.function, 0x10 + bar_index * 4)
}

/// Check whether a device+function exists (vendor ID != 0xFFFF).
fn device_exists(bus: u8, device: u8, function: u8) -> bool {
    let vendor = config_read32(bus, device, function, 0) & 0xFFFF;
    vendor != 0xFFFF
}

/// Probe one device/function and return a `PciDevice` if present.
fn probe(bus: u8, device: u8, function: u8) -> Option<PciDevice> {
    if !device_exists(bus, device, function) {
        return None;
    }
    let dw0 = config_read32(bus, device, function, 0);
    let dw2 = config_read32(bus, device, function, 8);
    let dw3 = config_read32(bus, device, function, 0xC);
    Some(PciDevice {
        bus,
        device,
        function,
        vendor_id: (dw0 & 0xFFFF) as u16,
        device_id: (dw0 >> 16) as u16,
        revision: (dw2 & 0xFF) as u8,
        prog_if: ((dw2 >> 8) & 0xFF) as u8,
        subclass: ((dw2 >> 16) & 0xFF) as u8,
        class: ((dw2 >> 24) & 0xFF) as u8,
        header_type: ((dw3 >> 16) & 0xFF) as u8,
    })
}

/// Enumerate all PCI devices on all buses.
pub fn enumerate() -> alloc::vec::Vec<PciDevice> {
    let mut devices = alloc::vec::Vec::new();
    for bus in 0u8..=255 {
        for device in 0u8..32 {
            if let Some(dev) = probe(bus, device, 0) {
                let multi_function = dev.header_type & 0x80 != 0;
                devices.push(dev);
                if multi_function {
                    for function in 1u8..8 {
                        if let Some(d) = probe(bus, device, function) {
                            devices.push(d);
                        }
                    }
                }
            }
        }
    }
    devices
}

/// Initialize the PCI subsystem: enumerate bus and log devices.
pub fn init() {
    let devices = enumerate();
    for dev in &devices {
        serial_println!(
            "[PCI] {:02x}:{:02x}.{} — vendor={:#06x} device={:#06x} class={:#04x}/{:#04x}",
            dev.bus, dev.device, dev.function,
            dev.vendor_id, dev.device_id,
            dev.class, dev.subclass
        );
    }
    serial_println!("[PCI] Found {} device(s)", devices.len());
}
