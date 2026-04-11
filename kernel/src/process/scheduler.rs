//! Round-robin process scheduler.

use core::sync::atomic::{AtomicU32, AtomicU64, Ordering};
use crate::serial_println;

/// Monotonic tick counter (incremented by the timer IRQ).
static TICK: AtomicU64 = AtomicU64::new(0);
/// PID of the currently running process.
static CURRENT_PID: AtomicU32 = AtomicU32::new(0);
/// Time-slice length in ticks.
const TIMESLICE: u64 = 10;

/// Initialize the scheduler.
pub fn init() {
    TICK.store(0, Ordering::SeqCst);
    CURRENT_PID.store(0, Ordering::SeqCst);
}

/// Called from the timer interrupt handler on every tick.
pub fn tick() {
    let t = TICK.fetch_add(1, Ordering::Relaxed) + 1;
    super::signal::dispatch_signals(current_pid());
    if t % TIMESLICE == 0 {
        schedule();
    }
}

/// Return the current monotonic tick.
pub fn ticks() -> u64 {
    TICK.load(Ordering::Relaxed)
}

/// Return the PID of the currently executing process.
pub fn current_pid() -> u32 {
    CURRENT_PID.load(Ordering::Relaxed)
}

/// Trigger a scheduling decision immediately.
pub fn force_schedule() {
    schedule();
}

/// Perform a round-robin context switch.
///
/// In a full implementation this saves the current register state and restores
/// the next runnable process. For now it just advances the PID pointer.
fn schedule() {
    use super::PROCESS_TABLE;
    use super::ProcessState;

    let table = PROCESS_TABLE.lock();
    let current = CURRENT_PID.load(Ordering::Relaxed);

    // Find the next ready process after the current one (wrapping).
    let n = table.len();
    if n == 0 {
        CURRENT_PID.store(0, Ordering::Relaxed);
        return;
    }

    let start = match table.iter().position(|p| p.pid == current) {
        Some(idx) => idx,
        None => {
            serial_println!("[SCHED] current PID {} not found; restarting from slot 0", current);
            0
        }
    };
    for i in 1..=n {
        let idx = (start + i) % n;
        if table[idx].state == ProcessState::Ready || table[idx].state == ProcessState::Running {
            CURRENT_PID.store(table[idx].pid, Ordering::Relaxed);
            return;
        }
    }
}
