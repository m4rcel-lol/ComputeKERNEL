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
use x86_64::instructions::{hlt, interrupts};

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
    interrupts::without_interrupts(|| {
        let current_pid = scheduler::current_pid();
        let mut table = PROCESS_TABLE.lock();
        let parent = table
            .iter()
            .find(|p| p.pid == current_pid)
            .ok_or("fork: current process not found")?;

        let child = Process {
            pid: NEXT_PID.fetch_add(1, Ordering::SeqCst),
            parent_pid: parent.pid,
            name: parent.name.clone(),
            state: ProcessState::Ready,
            priority: parent.priority,
            kernel_rsp: parent.kernel_rsp,
            exit_code: 0,
            pending_signals: 0,
        };

        let child_pid = child.pid;
        table.push(child);
        Ok(child_pid)
    })
}

/// Stub: execute a new program image.
pub fn execve(path: &str, _argv: &[&str], _envp: &[&str]) -> Result<(), &'static str> {
    interrupts::without_interrupts(|| {
        let current_pid = scheduler::current_pid();
        let mut table = PROCESS_TABLE.lock();
        let proc = table
            .iter_mut()
            .find(|p| p.pid == current_pid)
            .ok_or("execve: current process not found")?;

        let new_name = path.rsplit('/').next().unwrap_or(path);
        proc.name.clear();
        proc.name.push_str(new_name);
        proc.pending_signals = 0;
        Ok(())
    })
}

/// Exit the current process with the given exit code.
pub fn exit(code: i32) -> ! {
    interrupts::without_interrupts(|| {
        let current_pid = scheduler::current_pid();
        let mut table = PROCESS_TABLE.lock();

        if let Some(proc) = table.iter_mut().find(|p| p.pid == current_pid) {
            proc.state = ProcessState::Zombie;
            proc.exit_code = code;
        }

        for child in table.iter_mut() {
            if child.parent_pid == current_pid {
                child.parent_pid = 1;
            }
        }
    });

    serial_println!("[PROC] pid {} exiting with code {}", getpid(), code);
    scheduler::force_schedule();
    loop {
        hlt();
    }
}

/// Return the PID of the current (running) process.
pub fn getpid() -> u32 {
    scheduler::current_pid()
}

/// Return true if the current process is marked as zombie.
pub fn current_is_zombie() -> bool {
    interrupts::without_interrupts(|| {
        let pid = scheduler::current_pid();
        PROCESS_TABLE
            .lock()
            .iter()
            .find(|p| p.pid == pid)
            .map(|p| p.state == ProcessState::Zombie)
            .unwrap_or(false)
    })
}

/// Wait for a child to exit.
///
/// `target`:
/// - `None` waits for any child
/// - `Some(pid)` waits for a specific child
pub fn waitpid(target: Option<u32>) -> Result<(u32, i32), &'static str> {
    loop {
        let maybe_exit = interrupts::without_interrupts(|| {
            let mut table = PROCESS_TABLE.lock();
            let current_pid = scheduler::current_pid();

            let has_child = table.iter().any(|p| {
                p.parent_pid == current_pid
                    && target.map_or(true, |wanted| p.pid == wanted)
            });
            if !has_child {
                return Err("waitpid: no child");
            }

            if let Some((idx, _)) = table
                .iter()
                .enumerate()
                .find(|(_, p)| {
                    p.parent_pid == current_pid
                        && p.state == ProcessState::Zombie
                        && target.map_or(true, |wanted| p.pid == wanted)
                })
            {
                let pid = table[idx].pid;
                let code = table[idx].exit_code;
                table.remove(idx);
                return Ok(Some((pid, code)));
            }

            Ok(None)
        })?;

        if let Some((pid, code)) = maybe_exit {
            return Ok((pid, code));
        }
        hlt();
    }
}
