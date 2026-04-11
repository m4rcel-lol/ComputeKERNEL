//! VGA text-mode buffer driver (80×25 characters at physical 0xB8000).

use core::fmt;
use spin::Mutex;

/// Number of text columns in the VGA buffer.
pub const BUFFER_WIDTH: usize = 80;
/// Number of text rows in the VGA buffer.
pub const BUFFER_HEIGHT: usize = 25;

/// Physical address of the VGA text buffer.
const VGA_BUFFER_ADDR: usize = 0xB8000;

/// 4-bit colour codes used by VGA text mode.
#[allow(dead_code)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Color {
    Black = 0,
    Blue = 1,
    Green = 2,
    Cyan = 3,
    Red = 4,
    Magenta = 5,
    Brown = 6,
    LightGray = 7,
    DarkGray = 8,
    LightBlue = 9,
    LightGreen = 10,
    LightCyan = 11,
    LightRed = 12,
    Pink = 13,
    Yellow = 14,
    White = 15,
}

/// Combined foreground + background colour byte.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(transparent)]
struct ColorCode(u8);

impl ColorCode {
    const fn new(fg: Color, bg: Color) -> Self {
        ColorCode((bg as u8) << 4 | (fg as u8))
    }
}

/// A single character cell in the VGA buffer.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(C)]
struct ScreenChar {
    ascii_character: u8,
    color_code: ColorCode,
}

/// Kernel VGA writer — maintains cursor position and colour state.
pub struct Writer {
    column_position: usize,
    color_code: ColorCode,
    row: usize,
}

impl Writer {
    /// Write one byte to the buffer; handle newlines and scrolling.
    pub fn write_byte(&mut self, byte: u8) {
        match byte {
            b'\n' => self.new_line(),
            byte => {
                if self.column_position >= BUFFER_WIDTH {
                    self.new_line();
                }
                let row = self.row;
                let col = self.column_position;
                self.write_char_at(row, col, byte, self.color_code.0);
                self.column_position += 1;
            }
        }
    }

    /// Write a UTF-8 string (non-ASCII bytes become `0xFE`).
    pub fn write_string(&mut self, s: &str) {
        for byte in s.bytes() {
            match byte {
                0x20..=0x7e | b'\n' => self.write_byte(byte),
                _ => self.write_byte(0xFE),
            }
        }
    }

    fn new_line(&mut self) {
        self.column_position = 0;
        if self.row < BUFFER_HEIGHT - 1 {
            self.row += 1;
        } else {
            self.scroll();
        }
    }

    /// Scroll all rows up by one, clearing the last row.
    fn scroll(&mut self) {
        for row in 1..BUFFER_HEIGHT {
            for col in 0..BUFFER_WIDTH {
                let ch = self.read_char_at(row, col);
                self.write_raw_at(row - 1, col, ch);
            }
        }
        // Clear last row.
        for col in 0..BUFFER_WIDTH {
            self.write_char_at(BUFFER_HEIGHT - 1, col, b' ', self.color_code.0);
        }
    }

    /// Write a character cell directly to VGA memory (volatile write).
    #[inline]
    fn write_char_at(&self, row: usize, col: usize, ascii: u8, color: u8) {
        let offset = (row * BUFFER_WIDTH + col) * 2;
        unsafe {
            let ptr = (VGA_BUFFER_ADDR + offset) as *mut u8;
            ptr.write_volatile(ascii);
            ptr.add(1).write_volatile(color);
        }
    }

    /// Write a raw 16-bit VGA cell.
    #[inline]
    fn write_raw_at(&self, row: usize, col: usize, cell: u16) {
        let offset = (row * BUFFER_WIDTH + col) * 2;
        unsafe {
            let ptr = (VGA_BUFFER_ADDR + offset) as *mut u16;
            ptr.write_volatile(cell);
        }
    }

    /// Read a raw 16-bit VGA cell.
    #[inline]
    fn read_char_at(&self, row: usize, col: usize) -> u16 {
        let offset = (row * BUFFER_WIDTH + col) * 2;
        unsafe {
            let ptr = (VGA_BUFFER_ADDR + offset) as *const u16;
            ptr.read_volatile()
        }
    }

    /// Clear the entire screen.
    pub fn clear(&mut self) {
        for row in 0..BUFFER_HEIGHT {
            for col in 0..BUFFER_WIDTH {
                self.write_char_at(row, col, b' ', self.color_code.0);
            }
        }
        self.row = 0;
        self.column_position = 0;
    }
}

impl fmt::Write for Writer {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        self.write_string(s);
        Ok(())
    }
}

/// Global VGA writer instance.
pub static WRITER: Mutex<Writer> = Mutex::new(Writer {
    column_position: 0,
    color_code: ColorCode::new(Color::White, Color::Black),
    row: 0,
});

/// Initialize the VGA text mode buffer (clear screen).
pub fn init() {
    WRITER.lock().clear();
}

/// Internal print function — used by the `kprint!` macro.
#[doc(hidden)]
pub fn _kprint(args: fmt::Arguments) {
    use core::fmt::Write;
    use x86_64::instructions::interrupts;
    interrupts::without_interrupts(|| {
        WRITER.lock().write_fmt(args).unwrap();
    });
}

/// Print to the VGA text buffer without a newline.
#[macro_export]
macro_rules! kprint {
    ($($arg:tt)*) => ($crate::drivers::vga::_kprint(format_args!($($arg)*)));
}

/// Print to the VGA text buffer followed by a newline.
#[macro_export]
macro_rules! kprintln {
    () => ($crate::kprint!("\n"));
    ($fmt:expr) => ($crate::kprint!(concat!($fmt, "\n")));
    ($fmt:expr, $($arg:tt)*) => ($crate::kprint!(concat!($fmt, "\n"), $($arg)*));
}
