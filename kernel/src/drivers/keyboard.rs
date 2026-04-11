//! PS/2 keyboard driver using the `pc-keyboard` crate.

use crate::serial_print;
use pc_keyboard::{layouts, DecodedKey, HandleControl, Keyboard, ScancodeSet1};
use spin::{Lazy, Mutex};
use x86_64::instructions::port::Port;

/// PS/2 keyboard data port.
const KEYBOARD_DATA_PORT: u16 = 0x60;

/// Global keyboard state (lazy-initialized on first use).
static KEYBOARD: Lazy<Mutex<Keyboard<layouts::Us104Key, ScancodeSet1>>> =
    Lazy::new(|| {
        Mutex::new(Keyboard::new(
            ScancodeSet1::new(),
            layouts::Us104Key,
            HandleControl::Ignore,
        ))
    });

/// Initialize the keyboard driver.
pub fn init() {
    // Trigger lazy initialization and flush any pending scancode.
    let _kb = KEYBOARD.lock();
    let mut port: Port<u8> = Port::new(KEYBOARD_DATA_PORT);
    let _ = unsafe { port.read() };
}

/// Called from the keyboard IRQ handler to read and decode one scancode.
pub fn handle_interrupt() {
    let mut port: Port<u8> = Port::new(KEYBOARD_DATA_PORT);
    let scancode: u8 = unsafe { port.read() };

    let mut kb = KEYBOARD.lock();
    if let Ok(Some(key_event)) = kb.add_byte(scancode) {
        if let Some(key) = kb.process_keyevent(key_event) {
            match key {
                DecodedKey::Unicode(character) => {
                    serial_print!("{}", character);
                }
                DecodedKey::RawKey(key) => {
                    serial_print!("{:?}", key);
                }
            }
        }
    }
}
