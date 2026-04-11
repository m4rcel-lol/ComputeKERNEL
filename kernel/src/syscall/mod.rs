//! Linux-compatible system call layer.
//!
//! Dispatches system calls from user space to kernel handlers.
//! Numbers follow the Linux x86_64 ABI (`arch/x86/entry/syscalls/syscall_64.tbl`).

use crate::{kprint, serial_print};
use crate::process;

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
        SYS_KILL => sys_kill(frame.rdi as u32, frame.rsi as u8),
        SYS_GETPPID => sys_getppid(),
        SYS_BRK => sys_brk(frame.rdi),
        _ => Errno::NoSys as i64,
    }
}

// ── Individual syscall handlers ────────────────────────────────────────────

unsafe fn sys_read(fd: i32, buf: *mut u8, count: usize) -> i64 {
    // Stub: only handle fd=0 (stdin → serial).
    if fd == 0 {
        Errno::NoSys as i64 // stdin not yet wired to keyboard
    } else {
        Errno::BadF as i64
    }
}

unsafe fn sys_write(fd: i32, buf: *const u8, count: usize) -> i64 {
    if fd == 1 || fd == 2 {
        // stdout / stderr → forward to serial and VGA.
        let slice = core::slice::from_raw_parts(buf, count);
        if let Ok(s) = core::str::from_utf8(slice) {
            serial_print!("{}", s);
            kprint!("{}", s);
        }
        count as i64
    } else {
        Errno::BadF as i64
    }
}

unsafe fn sys_open(_path: *const u8, _flags: i32, _mode: u32) -> i64 {
    Errno::NoSys as i64
}

fn sys_close(_fd: i32) -> i64 {
    Errno::BadF as i64
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
    _path: *const u8,
    _argv: *const *const u8,
    _envp: *const *const u8,
) -> i64 {
    Errno::NoSys as i64
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

fn sys_brk(_addr: u64) -> i64 {
    // Stub: return current break address (not tracked yet).
    0
}
