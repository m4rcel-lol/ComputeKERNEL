//! Kernel-space interactive shell.
//!
//! Displays a fish/zsh-style prompt on the VGA text buffer and handles a set
//! of built-in commands. This runs in the kernel's main context after all
//! hardware initialisation is complete.

use crate::drivers::keyboard;
use crate::drivers::vga::{Color, WRITER};
use crate::{kprint, kprintln};
use x86_64::instructions::interrupts;

const MAX_CMD_LEN: usize = 256;
const HOSTNAME: &str = "computekernel";
const USERNAME: &str = "root";

// ── Public entry point ─────────────────────────────────────────────────────

/// Run the interactive shell. Never returns.
pub fn run() -> ! {
    kprintln!();
    kprintln!("Welcome to ComputeKERNEL v1.0.0");
    kprintln!("Type 'help' for available commands.");
    kprintln!();

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
    kprint!("{}@{}", USERNAME, HOSTNAME);
    set_color(Color::White, Color::Black);
    kprint!(" ~ ");
    set_color(Color::LightCyan, Color::Black);
    kprint!("% ");
    set_color(Color::White, Color::Black);
}

// ── Input reading ──────────────────────────────────────────────────────────

/// Block until a complete line is entered, filling `buf`. Returns byte count.
fn read_line(buf: &mut [u8; MAX_CMD_LEN]) -> usize {
    let mut pos = 0usize;

    loop {
        // Wait for a character; hlt() surrenders the CPU between keystrokes.
        let ch = loop {
            if let Some(c) = keyboard::try_read_char() {
                break c;
            }
            x86_64::instructions::hlt();
        };

        match ch {
            // Enter — commit the line.
            b'\n' | b'\r' => {
                kprintln!();
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
                kprintln!("^C");
                return 0;
            }
            // Printable ASCII.
            c if c >= 0x20 && pos < MAX_CMD_LEN - 1 => {
                buf[pos] = c;
                pos += 1;
                kprint!("{}", c as char);
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
            kprintln!("shell: command not found: {}", cmd);
            set_color(Color::White, Color::Black);
        }
    }
}

// ── Built-in commands ──────────────────────────────────────────────────────

fn cmd_help() {
    kprintln!("ComputeKERNEL built-in commands:");
    kprintln!("  help      - show this help");
    kprintln!("  clear     - clear the screen");
    kprintln!("  echo      - print arguments");
    kprintln!("  uname     - print system information");
    kprintln!("  version   - print kernel version");
    kprintln!("  ps        - list processes");
    kprintln!("  uptime    - show uptime in ticks");
    kprintln!("  halt      - halt the system");
    kprintln!("  reboot    - reboot the system");
}

fn cmd_clear() {
    interrupts::without_interrupts(|| WRITER.lock().clear());
}

fn cmd_echo(args: &str) {
    kprintln!("{}", args);
}

fn cmd_uname() {
    kprintln!("ComputeKERNEL v1.0.0 x86_64");
}

fn cmd_version() {
    kprintln!("ComputeKERNEL v1.0.0");
    kprintln!("Built with Rust (nightly)");
    kprintln!("Architecture: x86_64");
}

fn cmd_ps() {
    use crate::process::{ProcessState, PROCESS_TABLE};
    let table = PROCESS_TABLE.lock();
    kprintln!("  PID  STATE    NAME");
    for p in table.iter() {
        let state = match p.state {
            ProcessState::Ready   => "READY  ",
            ProcessState::Running => "RUNNING",
            ProcessState::Blocked => "BLOCKED",
            ProcessState::Zombie  => "ZOMBIE ",
        };
        kprintln!("  {:3}  {}  {}", p.pid, state, p.name);
    }
}

fn cmd_uptime() {
    let ticks = crate::process::scheduler::ticks();
    kprintln!("uptime: {} ticks", ticks);
}

fn cmd_halt() {
    kprintln!("Halting system...");
    x86_64::instructions::interrupts::disable();
    loop {
        x86_64::instructions::hlt();
    }
}

fn cmd_reboot() {
    kprintln!("Rebooting...");
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
