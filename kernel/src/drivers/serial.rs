//! UART 16550 serial port driver.
//!
//! Provides early-boot debug output over COM1 (I/O port 0x3F8).

use core::fmt;
use spin::Mutex;
use uart_16550::SerialPort;

/// COM1 base I/O port.
const COM1_PORT: u16 = 0x3F8;

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
