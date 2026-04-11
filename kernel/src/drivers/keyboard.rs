//! PS/2 keyboard driver using the `pc-keyboard` crate.

use crate::serial_print;
use pc_keyboard::{layouts, DecodedKey, HandleControl, Keyboard, KeyCode, ScancodeSet1};
use spin::{Lazy, Mutex};
use x86_64::instructions::port::Port;

/// PS/2 keyboard data port.
const KEYBOARD_DATA_PORT: u16 = 0x60;
/// PS/2 keyboard controller status port.
const KEYBOARD_STATUS_PORT: u16 = 0x64;
/// Output buffer full bit in keyboard controller status register.
const STATUS_OUTPUT_BUFFER_FULL: u8 = 0x01;

/// Capacity of the keyboard input ring buffer.
const KEY_BUF_SIZE: usize = 256;

/// Simple ring buffer for decoded key characters.
struct KeyBuffer {
    buf: [u8; KEY_BUF_SIZE],
    read: usize,
    write: usize,
    count: usize,
}

impl KeyBuffer {
    const fn new() -> Self {
        KeyBuffer { buf: [0u8; KEY_BUF_SIZE], read: 0, write: 0, count: 0 }
    }

    fn push(&mut self, byte: u8) {
        if self.count < KEY_BUF_SIZE {
            self.buf[self.write] = byte;
            self.write = (self.write + 1) % KEY_BUF_SIZE;
            self.count += 1;
        }
    }

    fn pop(&mut self) -> Option<u8> {
        if self.count == 0 {
            return None;
        }
        let byte = self.buf[self.read];
        self.read = (self.read + 1) % KEY_BUF_SIZE;
        self.count -= 1;
        Some(byte)
    }
}

/// Global keyboard input buffer — written by the IRQ handler, read by the shell.
static KEY_BUFFER: Mutex<KeyBuffer> = Mutex::new(KeyBuffer::new());

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

/// Try to read one decoded character from the keyboard buffer.
///
/// Returns `None` if the buffer is empty (non-blocking).
/// Disables interrupts while the buffer lock is held to prevent a deadlock
/// with the keyboard IRQ handler which also writes to the same buffer.
pub fn try_read_char() -> Option<u8> {
    x86_64::instructions::interrupts::without_interrupts(|| KEY_BUFFER.lock().pop())
}

/// Poll the keyboard controller directly and decode one available key.
///
/// This is a fallback path for environments where IRQ keyboard input is not
/// delivered reliably; it allows the shell to remain interactive via polling.
pub fn poll_char() -> Option<u8> {
    let mut status_port: Port<u8> = Port::new(KEYBOARD_STATUS_PORT);
    let mut data_port: Port<u8> = Port::new(KEYBOARD_DATA_PORT);

    x86_64::instructions::interrupts::without_interrupts(|| {
        let status = unsafe { status_port.read() };
        if status & STATUS_OUTPUT_BUFFER_FULL == 0 {
            return None;
        }

        let scancode = unsafe { data_port.read() };
        decode_scancode(scancode)
    })
}

/// Called from the keyboard IRQ handler to read and decode one scancode.
pub fn handle_interrupt() {
    let mut port: Port<u8> = Port::new(KEYBOARD_DATA_PORT);
    let scancode: u8 = unsafe { port.read() };

    if let Some(byte) = decode_scancode(scancode) {
        KEY_BUFFER.lock().push(byte);
    }
}

fn decode_scancode(scancode: u8) -> Option<u8> {
    let mut kb = KEYBOARD.lock();
    if let Ok(Some(key_event)) = kb.add_byte(scancode) {
        if let Some(key) = kb.process_keyevent(key_event) {
            match key {
                DecodedKey::Unicode(character) => {
                    serial_print!("{}", character);
                    return Some(character as u8);
                }
                DecodedKey::RawKey(key) => {
                    serial_print!("{:?} ", key);
                    match key {
                        KeyCode::Backspace => return Some(0x08),
                        KeyCode::Delete => return Some(0x7F),
                        _ => return None,
                    }
                }
            }
        }
    }
    None
}
