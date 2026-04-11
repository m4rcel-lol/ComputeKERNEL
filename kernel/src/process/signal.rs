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
    let mut table = PROCESS_TABLE.lock();
    if let Some(proc) = table.iter_mut().find(|p| p.pid == pid) {
        proc.pending_signals |= 1 << sig;
        Ok(())
    } else {
        Err("kill: process not found")
    }
}

/// Stub: handle pending signals for the current process.
pub fn dispatch_signals(_pid: u32) {
    // TODO: check pending_signals mask, call user-space handlers or defaults.
}
