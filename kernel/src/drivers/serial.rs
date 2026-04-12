//! UART 16550 serial port driver.
//!
//! Provides early-boot debug output over COM1 (I/O port 0x3F8).

use core::fmt;
use spin::Mutex;
use uart_16550::SerialPort;
use x86_64::instructions::port::Port;

/// COM1 base I/O port.
const COM1_PORT: u16 = 0x3F8;
/// COM1 data register port.
const COM1_DATA_PORT: u16 = COM1_PORT;
/// COM1 line status register port.
const COM1_LINE_STATUS_PORT: u16 = COM1_PORT + 5;
/// Data ready bit in COM1 line status register.
const LINE_STATUS_DATA_READY: u8 = 1 << 0;

static SERIAL1: Mutex<SerialPort> = Mutex::new(unsafe { SerialPort::new(COM1_PORT) });

/// Initialize COM1 serial port.
pub fn init() {
    SERIAL1.lock().init();
}

/// Internal print function — formats `args` and writes to serial.
#[doc(hidden)]
pub fn _serial_print(args: fmt::Arguments) {
    use core::fmt::Write;
    use x86_64::instructions::interrupts;

    // Disable interrupts while holding the lock to avoid deadlock on the
    // timer handler trying to print.
    interrupts::without_interrupts(|| {
        SERIAL1
            .lock()
            .write_fmt(args)
            .expect("serial write failed");
    });
}

/// Try to read one byte from COM1 without blocking.
pub fn try_read_byte() -> Option<u8> {
    x86_64::instructions::interrupts::without_interrupts(|| {
        let mut status_port: Port<u8> = Port::new(COM1_LINE_STATUS_PORT);
        let status = unsafe { status_port.read() };
        if status & LINE_STATUS_DATA_READY == 0 {
            return None;
        }
        let mut data_port: Port<u8> = Port::new(COM1_DATA_PORT);
        Some(unsafe { data_port.read() })
    })
}

/// Print to COM1 without a newline.
#[macro_export]
macro_rules! serial_print {
    ($($arg:tt)*) => {
        $crate::drivers::serial::_serial_print(format_args!($($arg)*))
    };
}

/// Print to COM1 followed by a newline.
#[macro_export]
macro_rules! serial_println {
    () => ($crate::serial_print!("\n"));
    ($fmt:expr) => ($crate::serial_print!(concat!($fmt, "\n")));
    ($fmt:expr, $($arg:tt)*) => (
        $crate::serial_print!(concat!($fmt, "\n"), $($arg)*)
    );
}
