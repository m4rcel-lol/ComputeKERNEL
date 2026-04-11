//! Process management subsystem.
//!
//! Provides the process control block (PCB), a round-robin scheduler,
//! and stubs for fork/exec/signal handling.

pub mod scheduler;
pub mod signal;

use crate::serial_println;
use alloc::{string::String, vec::Vec};
use core::sync::atomic::{AtomicU32, Ordering};
use spin::Mutex;

/// Next PID to allocate (atomic counter).
static NEXT_PID: AtomicU32 = AtomicU32::new(1);

/// Process state.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProcessState {
    Ready,
    Running,
    Blocked,
    Zombie,
}

/// Process priority level (higher == more CPU time).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u8)]
pub enum Priority {
    Idle = 0,
    Low = 1,
    Normal = 2,
    High = 3,
    RealTime = 4,
}

/// Process Control Block.
#[derive(Debug)]
pub struct Process {
    pub pid: u32,
    pub parent_pid: u32,
    pub name: String,
    pub state: ProcessState,
    pub priority: Priority,
    /// Saved kernel stack pointer (used during context switch).
    pub kernel_rsp: u64,
    /// Exit code (set when state == Zombie).
    pub exit_code: i32,
    /// Pending signal mask.
    pub pending_signals: u32,
}

impl Process {
    /// Create a new process with the given name and parent.
    pub fn new(name: &str, parent_pid: u32) -> Self {
        let pid = NEXT_PID.fetch_add(1, Ordering::SeqCst);
        Process {
            pid,
            parent_pid,
            name: String::from(name),
            state: ProcessState::Ready,
            priority: Priority::Normal,
            kernel_rsp: 0,
            exit_code: 0,
            pending_signals: 0,
        }
    }
}

/// Global process table.
pub static PROCESS_TABLE: Mutex<Vec<Process>> = Mutex::new(Vec::new());

/// Initialize the process subsystem and create the idle/init processes.
pub fn init() {
    let mut table = PROCESS_TABLE.lock();

    // PID 0 — idle process.
    table.push(Process {
        pid: 0,
        parent_pid: 0,
        name: String::from("idle"),
        state: ProcessState::Running,
        priority: Priority::Idle,
        kernel_rsp: 0,
        exit_code: 0,
        pending_signals: 0,
    });

    // PID 1 — init process.
    let init = Process::new("init", 0);
    table.push(init);

    scheduler::init();
}

/// Stub: fork the current process.
///
/// Returns the child PID to the parent, and 0 to the child.
pub fn fork() -> Result<u32, &'static str> {
    Err("fork: not yet implemented")
}

/// Stub: execute a new program image.
pub fn execve(_path: &str, _argv: &[&str], _envp: &[&str]) -> Result<(), &'static str> {
    Err("execve: not yet implemented")
}

/// Exit the current process with the given exit code.
pub fn exit(code: i32) -> ! {
    // In a real kernel this would clean up resources and context-switch.
    serial_println!("[PROC] Process exiting with code {}", code);
    loop {
        x86_64::instructions::hlt();
    }
}

/// Return the PID of the current (running) process.
pub fn getpid() -> u32 {
    scheduler::current_pid()
}
