//! PID 1 — init process.
//!
//! This is the first user-space process started by the kernel.
//! It mounts essential filesystems and eventually spawns a shell.

use std::process::Command;

fn main() {
    eprintln!("[init] PID 1 starting");

    // Mount virtual filesystems.
    mount("proc", "/proc", "proc");
    mount("sysfs", "/sys", "sysfs");
    mount("devtmpfs", "/dev", "devtmpfs");

    eprintln!("[init] filesystems mounted");

    // Attempt to start a shell.
    loop {
        eprintln!("[init] starting /bin/sh");
        match Command::new("/bin/sh").status() {
            Ok(status) => {
                eprintln!("[init] /bin/sh exited with {}", status);
            }
            Err(e) => {
                eprintln!("[init] failed to start /bin/sh: {}", e);
                // Back off before retrying.
                std::thread::sleep(std::time::Duration::from_secs(1));
            }
        }
    }
}

/// Stub mount wrapper (calls the `mount` syscall via a shell command until
/// a native Rust binding is implemented).
fn mount(fstype: &str, target: &str, options: &str) {
    let _ = Command::new("mount")
        .args(["-t", fstype, "none", target, "-o", options])
        .status();
    eprintln!("[init] mounted {} on {}", fstype, target);
}
