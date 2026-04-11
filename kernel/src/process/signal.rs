//! Signal handling stubs.
//!
//! Provides POSIX-compatible signal constants and delivery stubs.

/// POSIX signal numbers.
pub const SIGHUP: u8 = 1;
pub const SIGINT: u8 = 2;
pub const SIGQUIT: u8 = 3;
pub const SIGILL: u8 = 4;
pub const SIGTRAP: u8 = 5;
pub const SIGABRT: u8 = 6;
pub const SIGFPE: u8 = 8;
pub const SIGKILL: u8 = 9;
pub const SIGSEGV: u8 = 11;
pub const SIGPIPE: u8 = 13;
pub const SIGALRM: u8 = 14;
pub const SIGTERM: u8 = 15;
pub const SIGCHLD: u8 = 17;
pub const SIGSTOP: u8 = 19;
pub const SIGCONT: u8 = 18;

/// Signal disposition (action taken when a signal is delivered).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SigAction {
    /// Default kernel action (terminate, core-dump, ignore, stop, continue).
    Default,
    /// Ignore the signal.
    Ignore,
    /// User-space handler address.
    Handler(u64),
}

/// Stub: send signal `sig` to process `pid`.
pub fn kill(pid: u32, sig: u8) -> Result<(), &'static str> {
    use super::PROCESS_TABLE;
    if sig > 31 {
        return Err("kill: invalid signal");
    }
    x86_64::instructions::interrupts::without_interrupts(|| {
        let mut table = PROCESS_TABLE.lock();
        if let Some(proc) = table.iter_mut().find(|p| p.pid == pid) {
            if sig != 0 {
                proc.pending_signals |= 1u32 << sig;
            }
            Ok(())
        } else {
            Err("kill: process not found")
        }
    })
}

/// Handle pending signals for the given process with default kernel actions.
pub fn dispatch_signals(pid: u32) {
    use super::{ProcessState, PROCESS_TABLE};

    x86_64::instructions::interrupts::without_interrupts(|| {
        let mut table = PROCESS_TABLE.lock();
        let Some(proc) = table.iter_mut().find(|p| p.pid == pid) else {
            return;
        };

        let pending = proc.pending_signals;
        if pending == 0 {
            return;
        }

        let terminating_sig = if pending & (1u32 << SIGKILL) != 0 {
            Some(SIGKILL)
        } else if pending & (1u32 << SIGTERM) != 0 {
            Some(SIGTERM)
        } else if pending & (1u32 << SIGINT) != 0 {
            Some(SIGINT)
        } else if pending & (1u32 << SIGQUIT) != 0 {
            Some(SIGQUIT)
        } else {
            None
        };

        if let Some(sig) = terminating_sig {
            proc.state = ProcessState::Zombie;
            proc.exit_code = 128 + sig as i32;
            proc.pending_signals = 0;
            return;
        }

        // Ignore non-terminating signals in this baseline implementation.
        proc.pending_signals = 0;
    });
}
