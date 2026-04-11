//! Linux-compatible system call layer.
//!
//! Dispatches system calls from user space to kernel handlers.
//! Numbers follow the Linux x86_64 ABI (`arch/x86/entry/syscalls/syscall_64.tbl`).

use crate::{kprint, serial_print};
use alloc::{format, string::String, vec::Vec};
use crate::process;
use spin::{Lazy, Mutex};
use x86_64::instructions::hlt;

// ── System call numbers (Linux x86_64 ABI) ────────────────────────────────

pub const SYS_READ: u64 = 0;
pub const SYS_WRITE: u64 = 1;
pub const SYS_OPEN: u64 = 2;
pub const SYS_CLOSE: u64 = 3;
pub const SYS_STAT: u64 = 4;
pub const SYS_FSTAT: u64 = 5;
pub const SYS_LSTAT: u64 = 6;
pub const SYS_POLL: u64 = 7;
pub const SYS_LSEEK: u64 = 8;
pub const SYS_MMAP: u64 = 9;
pub const SYS_MPROTECT: u64 = 10;
pub const SYS_MUNMAP: u64 = 11;
pub const SYS_BRK: u64 = 12;
pub const SYS_RT_SIGACTION: u64 = 13;
pub const SYS_RT_SIGPROCMASK: u64 = 14;
pub const SYS_IOCTL: u64 = 16;
pub const SYS_GETPID: u64 = 39;
pub const SYS_FORK: u64 = 57;
pub const SYS_VFORK: u64 = 58;
pub const SYS_EXECVE: u64 = 59;
pub const SYS_EXIT: u64 = 60;
pub const SYS_WAIT4: u64 = 61;
pub const SYS_KILL: u64 = 62;
pub const SYS_GETPPID: u64 = 110;
pub const SYS_EXIT_GROUP: u64 = 231;

const O_WRONLY: i32 = 0x1;
const O_RDWR: i32 = 0x2;
const O_ACCMODE: i32 = 0x3;

#[derive(Debug, Clone, Copy)]
enum PseudoNode {
    ProcVersion,
    ProcUptime,
    ProcMeminfo,
    SysHostname,
    DevNull,
    DevZero,
    DevFull,
    DevTtyS0,
}

#[derive(Debug, Clone, Copy)]
struct OpenFile {
    node: PseudoNode,
    offset: u64,
}

static FD_TABLE: Lazy<Mutex<Vec<Option<OpenFile>>>> = Lazy::new(|| Mutex::new(Vec::new()));

/// Error codes (negated errno values returned from syscall handlers).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i64)]
pub enum Errno {
    Success = 0,
    Perm = -1,
    NoEnt = -2,
    Srch = -3,
    Intr = -4,
    Io = -5,
    BadF = -9,
    NoMem = -12,
    Acces = -13,
    Fault = -14,
    Inval = -22,
    NFile = -23,
    MFile = -24,
    NoSpc = -28,
    NoSys = -38,
}

/// System call context: register values at the point of the `syscall` instruction.
#[derive(Debug, Clone, Copy)]
#[repr(C)]
pub struct SyscallFrame {
    /// Syscall number (rax).
    pub num: u64,
    /// Arguments.
    pub rdi: u64,
    pub rsi: u64,
    pub rdx: u64,
    pub r10: u64,
    pub r8: u64,
    pub r9: u64,
}

/// Dispatch a system call and return the result in rax.
///
/// # Safety
/// The register values must come from a valid user-space `syscall` instruction.
pub unsafe fn dispatch(frame: &SyscallFrame) -> i64 {
    match frame.num {
        SYS_READ => sys_read(frame.rdi as i32, frame.rsi as *mut u8, frame.rdx as usize),
        SYS_WRITE => sys_write(frame.rdi as i32, frame.rsi as *const u8, frame.rdx as usize),
        SYS_OPEN => sys_open(frame.rdi as *const u8, frame.rsi as i32, frame.rdx as u32),
        SYS_CLOSE => sys_close(frame.rdi as i32),
        SYS_GETPID => sys_getpid(),
        SYS_FORK => sys_fork(),
        SYS_EXECVE => sys_execve(
            frame.rdi as *const u8,
            frame.rsi as *const *const u8,
            frame.rdx as *const *const u8,
        ),
        SYS_EXIT | SYS_EXIT_GROUP => sys_exit(frame.rdi as i32),
        SYS_WAIT4 => sys_wait4(
            frame.rdi as i32,
            frame.rsi as *mut i32,
            frame.rdx as i32,
            frame.r10 as u64,
        ),
        SYS_KILL => sys_kill(frame.rdi as u32, frame.rsi as u8),
        SYS_GETPPID => sys_getppid(),
        SYS_BRK => sys_brk(frame.rdi),
        _ => Errno::NoSys as i64,
    }
}

// ── Individual syscall handlers ────────────────────────────────────────────

unsafe fn sys_read(fd: i32, buf: *mut u8, count: usize) -> i64 {
    if count == 0 {
        return 0;
    }
    if buf.is_null() {
        return Errno::Fault as i64;
    }

    let out = core::slice::from_raw_parts_mut(buf, count);

    if fd == 0 {
        let mut read = 0usize;

        while read == 0 {
            if let Some(ch) = crate::drivers::keyboard::try_read_char()
                .or_else(|| crate::drivers::keyboard::poll_char())
            {
                out[read] = ch;
                read += 1;
                break;
            }
            hlt();
        }

        while read < out.len() {
            let Some(ch) = crate::drivers::keyboard::try_read_char()
                .or_else(|| crate::drivers::keyboard::poll_char()) else {
                break;
            };
            out[read] = ch;
            read += 1;
            if ch == b'\n' || ch == b'\r' {
                break;
            }
        }
        return read as i64;
    }

    let mut table = FD_TABLE.lock();
    let Some(slot) = table.get_mut(fd as usize - 3) else {
        return Errno::BadF as i64;
    };
    let Some(file) = slot.as_mut() else {
        return Errno::BadF as i64;
    };

    let n = match file.node {
        PseudoNode::ProcVersion => read_static(out, file.offset, b"ComputeKERNEL version 1.0.0 (Rust nightly)\n"),
        PseudoNode::ProcUptime => {
            let ticks = crate::process::scheduler::ticks();
            let seconds = ticks / 100;
            let content = format!("{}.00 {}.00\n", seconds, seconds);
            read_dynamic(out, file.offset, content.as_bytes())
        }
        PseudoNode::ProcMeminfo => {
            read_static(out, file.offset, b"MemTotal:         102400 kB\nMemFree:           51200 kB\n")
        }
        PseudoNode::SysHostname => read_static(out, file.offset, b"computekernel\n"),
        PseudoNode::DevNull => 0,
        PseudoNode::DevZero | PseudoNode::DevFull => {
            out.fill(0);
            out.len()
        }
        PseudoNode::DevTtyS0 => 0,
    };

    file.offset = file.offset.saturating_add(n as u64);
    n as i64
}

fn read_static(buf: &mut [u8], offset: u64, content: &[u8]) -> usize {
    let start = offset as usize;
    if start >= content.len() {
        return 0;
    }
    let available = &content[start..];
    let n = available.len().min(buf.len());
    buf[..n].copy_from_slice(&available[..n]);
    n
}

fn read_dynamic(buf: &mut [u8], offset: u64, content: &[u8]) -> usize {
    read_static(buf, offset, content)
}

unsafe fn sys_write(fd: i32, buf: *const u8, count: usize) -> i64 {
    if count == 0 {
        return 0;
    }
    if buf.is_null() {
        return Errno::Fault as i64;
    }

    if fd == 1 || fd == 2 {
        // stdout / stderr → forward to serial and VGA.
        let slice = core::slice::from_raw_parts(buf, count);
        if let Ok(s) = core::str::from_utf8(slice) {
            serial_print!("{}", s);
            kprint!("{}", s);
        }
        count as i64
    } else {
        let slice = core::slice::from_raw_parts(buf, count);
        let mut table = FD_TABLE.lock();
        let Some(slot) = table.get_mut(fd as usize - 3) else {
            return Errno::BadF as i64;
        };
        let Some(file) = slot.as_mut() else {
            return Errno::BadF as i64;
        };

        match file.node {
            PseudoNode::DevNull | PseudoNode::DevZero => count as i64,
            PseudoNode::DevFull => Errno::NoSpc as i64,
            PseudoNode::DevTtyS0 => {
                for &byte in slice {
                    crate::drivers::serial::_serial_print(format_args!("{}", byte as char));
                }
                count as i64
            }
            _ => Errno::Perm as i64,
        }
    }
}

unsafe fn sys_open(path: *const u8, flags: i32, _mode: u32) -> i64 {
    if path.is_null() {
        return Errno::Fault as i64;
    }

    let Ok(path) = read_user_cstr(path, 256) else {
        return Errno::Fault as i64;
    };

    let node = match path.as_str() {
        "/proc/version" => PseudoNode::ProcVersion,
        "/proc/uptime" => PseudoNode::ProcUptime,
        "/proc/meminfo" => PseudoNode::ProcMeminfo,
        "/sys/kernel/hostname" => PseudoNode::SysHostname,
        "/dev/null" => PseudoNode::DevNull,
        "/dev/zero" => PseudoNode::DevZero,
        "/dev/full" => PseudoNode::DevFull,
        "/dev/ttyS0" => PseudoNode::DevTtyS0,
        _ => return Errno::NoEnt as i64,
    };

    let access = flags & O_ACCMODE;
    let write_requested = access == O_WRONLY || access == O_RDWR;
    if write_requested {
        match node {
            PseudoNode::ProcVersion
            | PseudoNode::ProcUptime
            | PseudoNode::ProcMeminfo
            | PseudoNode::SysHostname => return Errno::Acces as i64,
            _ => {}
        }
    }

    let mut table = FD_TABLE.lock();
    let file = OpenFile { node, offset: 0 };
    for (idx, slot) in table.iter_mut().enumerate() {
        if slot.is_none() {
            *slot = Some(file);
            return (idx as i64) + 3;
        }
    }
    table.push(Some(file));
    (table.len() as i64 - 1) + 3
}

fn sys_close(fd: i32) -> i64 {
    if fd < 3 {
        return 0;
    }
    let mut table = FD_TABLE.lock();
    let Some(slot) = table.get_mut(fd as usize - 3) else {
        return Errno::BadF as i64;
    };
    if slot.take().is_some() {
        0
    } else {
        Errno::BadF as i64
    }
}

fn sys_getpid() -> i64 {
    process::getpid() as i64
}

fn sys_getppid() -> i64 {
    let table = process::PROCESS_TABLE.lock();
    let pid = process::getpid();
    table
        .iter()
        .find(|p| p.pid == pid)
        .map(|p| p.parent_pid as i64)
        .unwrap_or(Errno::Srch as i64)
}

fn sys_fork() -> i64 {
    match process::fork() {
        Ok(child_pid) => child_pid as i64,
        Err(_) => Errno::NoSys as i64,
    }
}

unsafe fn sys_execve(
    path: *const u8,
    _argv: *const *const u8,
    _envp: *const *const u8,
) -> i64 {
    if path.is_null() {
        return Errno::Fault as i64;
    }

    let Ok(path) = read_user_cstr(path, 256) else {
        return Errno::Fault as i64;
    };
    match process::execve(path.as_str(), &[], &[]) {
        Ok(()) => 0,
        Err(_) => Errno::NoEnt as i64,
    }
}

fn sys_exit(code: i32) -> i64 {
    process::exit(code);
}

fn sys_kill(pid: u32, sig: u8) -> i64 {
    match process::signal::kill(pid, sig) {
        Ok(()) => 0,
        Err(_) => Errno::Srch as i64,
    }
}

fn sys_wait4(pid: i32, status: *mut i32, _options: i32, _rusage: u64) -> i64 {
    let target = if pid <= 0 { None } else { Some(pid as u32) };
    match process::waitpid(target) {
        Ok((child_pid, code)) => {
            if !status.is_null() {
                unsafe { *status = (code & 0xff) << 8; }
            }
            child_pid as i64
        }
        Err(_) => Errno::Srch as i64,
    }
}

fn sys_brk(_addr: u64) -> i64 {
    // Stub: return current break address (not tracked yet).
    0
}

unsafe fn read_user_cstr(ptr: *const u8, max_len: usize) -> Result<String, ()> {
    if ptr.is_null() {
        return Err(());
    }

    let mut bytes = Vec::new();
    for i in 0..max_len {
        let b = *ptr.add(i);
        if b == 0 {
            return String::from_utf8(bytes).map_err(|_| ());
        }
        bytes.push(b);
    }
    Err(())
}
