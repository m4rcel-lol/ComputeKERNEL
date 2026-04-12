//! Kernel-space interactive shell.
//!
//! Displays a fish/zsh-style prompt on the VGA text buffer and handles a set
//! of built-in commands. This runs in the kernel's main context after all
//! hardware initialisation is complete.

use crate::drivers::keyboard;
use crate::drivers::serial;
use crate::drivers::vga::{Color, WRITER};
use crate::{kprint, kprintln, serial_print, serial_println};
use x86_64::instructions::interrupts;

const MAX_CMD_LEN: usize = 256;
const HOSTNAME: &str = "computekernel";
const USERNAME: &str = "root";

macro_rules! console_print {
    ($($arg:tt)*) => {{
        kprint!($($arg)*);
        serial_print!($($arg)*);
    }};
}

macro_rules! console_println {
    () => {{
        kprintln!();
        serial_println!();
    }};
    ($fmt:expr) => {{
        kprintln!($fmt);
        serial_println!($fmt);
    }};
    ($fmt:expr, $($arg:tt)*) => {{
        kprintln!($fmt, $($arg)*);
        serial_println!($fmt, $($arg)*);
    }};
}

// ── Public entry point ─────────────────────────────────────────────────────

/// Run the interactive shell. Never returns.
pub fn run() -> ! {
    console_println!();
    console_println!("Welcome to ComputeKERNEL v1.0.0");
    console_println!("Type 'help' for available commands.");
    console_println!();

    loop {
        print_prompt();
        let mut buf = [0u8; MAX_CMD_LEN];
        let len = read_line(&mut buf);
        execute(&buf[..len]);
    }
}

// ── Prompt ─────────────────────────────────────────────────────────────────

/// Print a fish/zsh-style coloured prompt.
fn print_prompt() {
    set_color(Color::LightGreen, Color::Black);
    console_print!("{}@{}", USERNAME, HOSTNAME);
    set_color(Color::White, Color::Black);
    console_print!(" ~ ");
    set_color(Color::LightCyan, Color::Black);
    console_print!("% ");
    set_color(Color::White, Color::Black);
}

// ── Input reading ──────────────────────────────────────────────────────────

/// Block until a complete line is entered, filling `buf`. Returns byte count.
fn read_line(buf: &mut [u8; MAX_CMD_LEN]) -> usize {
    let mut pos = 0usize;

    loop {
        // Wait for a character; hlt() surrenders the CPU between keystrokes.
        let ch = loop {
            if let Some(c) = keyboard::try_read_char()
                .or_else(|| serial::try_read_byte())
                .or_else(|| keyboard::poll_char())
            {
                break c;
            }
            x86_64::instructions::hlt();
        };

        match ch {
            // Enter — commit the line.
            b'\n' | b'\r' => {
                console_println!();
                break;
            }
            // Backspace (^H) or DEL.
            0x08 | 0x7F => {
                if pos > 0 {
                    pos -= 1;
                    buf[pos] = 0;
                    interrupts::without_interrupts(|| WRITER.lock().backspace());
                }
            }
            // Ctrl+C — cancel the current line.
            0x03 => {
                console_println!("^C");
                return 0;
            }
            // Printable ASCII.
            c if c >= 0x20 && pos < MAX_CMD_LEN - 1 => {
                buf[pos] = c;
                pos += 1;
                console_print!("{}", c as char);
            }
            _ => {}
        }
    }

    pos
}

// ── Command dispatch ───────────────────────────────────────────────────────

fn execute(raw: &[u8]) {
    let line = core::str::from_utf8(raw).unwrap_or("").trim();
    if line.is_empty() {
        return;
    }

    let (cmd, args) = match line.find(' ') {
        Some(i) => (&line[..i], line[i + 1..].trim()),
        None    => (line, ""),
    };

    match cmd {
        "help"               => cmd_help(),
        "clear"              => cmd_clear(),
        "echo"               => cmd_echo(args),
        "uname"              => cmd_uname(),
        "version"            => cmd_version(),
        "ps"                 => cmd_ps(),
        "uptime"             => cmd_uptime(),
        "halt" | "poweroff"  => cmd_halt(),
        "reboot"             => cmd_reboot(),
        _ => {
            set_color(Color::LightRed, Color::Black);
            console_println!("shell: command not found: {}", cmd);
            set_color(Color::White, Color::Black);
        }
    }
}

// ── Built-in commands ──────────────────────────────────────────────────────

fn cmd_help() {
    console_println!("ComputeKERNEL built-in commands:");
    console_println!("  help      - show this help");
    console_println!("  clear     - clear the screen");
    console_println!("  echo      - print arguments");
    console_println!("  uname     - print system information");
    console_println!("  version   - print kernel version");
    console_println!("  ps        - list processes");
    console_println!("  uptime    - show uptime in ticks");
    console_println!("  halt      - halt the system");
    console_println!("  reboot    - reboot the system");
}

fn cmd_clear() {
    interrupts::without_interrupts(|| WRITER.lock().clear());
}

fn cmd_echo(args: &str) {
    console_println!("{}", args);
}

fn cmd_uname() {
    console_println!("ComputeKERNEL v1.0.0 x86_64");
}

fn cmd_version() {
    console_println!("ComputeKERNEL v1.0.0");
    console_println!("Built with Rust (nightly)");
    console_println!("Architecture: x86_64");
}

fn cmd_ps() {
    use crate::process::{ProcessState, PROCESS_TABLE};
    let table = PROCESS_TABLE.lock();
    console_println!("  PID  STATE    NAME");
    for p in table.iter() {
        let state = match p.state {
            ProcessState::Ready   => "READY  ",
            ProcessState::Running => "RUNNING",
            ProcessState::Blocked => "BLOCKED",
            ProcessState::Zombie  => "ZOMBIE ",
        };
        console_println!("  {:3}  {}  {}", p.pid, state, p.name);
    }
}

fn cmd_uptime() {
    let ticks = crate::process::scheduler::ticks();
    console_println!("uptime: {} ticks", ticks);
}

fn cmd_halt() {
    console_println!("Halting system...");
    x86_64::instructions::interrupts::disable();
    loop {
        x86_64::instructions::hlt();
    }
}

fn cmd_reboot() {
    console_println!("Rebooting...");
    // Pulse the keyboard controller's reset line (port 0x64, command 0xFE).
    unsafe {
        let mut port: x86_64::instructions::port::Port<u8> =
            x86_64::instructions::port::Port::new(0x64);
        port.write(0xFEu8);
    }
    // Spin if the reset doesn't take immediately.
    loop {
        x86_64::instructions::hlt();
    }
}

// ── Helper ─────────────────────────────────────────────────────────────────

#[inline]
fn set_color(fg: Color, bg: Color) {
    interrupts::without_interrupts(|| WRITER.lock().set_color(fg, bg));
}
