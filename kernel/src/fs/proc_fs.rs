//! /proc filesystem — exposes kernel process information.

use alloc::{format, string::String, vec, vec::Vec};

use super::vfs::{DirEntry, FileMode, FileSystem, Inode, InodeKind, VfsError};

/// In-memory /proc filesystem.
pub struct ProcFs;

impl ProcFs {
    pub fn new() -> Self {
        ProcFs
    }

    /// Generate the content of /proc/version.
    fn version_content() -> Vec<u8> {
        b"ComputeKERNEL version 1.0.0 (Rust nightly)\n".to_vec()
    }

    /// Generate the content of /proc/uptime.
    fn uptime_content() -> Vec<u8> {
        let ticks = crate::process::scheduler::ticks();
        let seconds = ticks / 100; // assuming 100 Hz timer
        format!("{}.00 {}.00\n", seconds, seconds).into_bytes()
    }

    /// Generate the content of /proc/meminfo.
    fn meminfo_content() -> Vec<u8> {
        b"MemTotal:         102400 kB\nMemFree:           51200 kB\n".to_vec()
    }
}

impl FileSystem for ProcFs {
    fn name(&self) -> &str {
        "proc"
    }

    fn lookup(&self, path: &str) -> Result<Inode, VfsError> {
        match path {
            "/" | "" => Ok(Inode::new(1, InodeKind::Directory)),
            "/version" => Ok(Inode::new(2, InodeKind::RegularFile)),
            "/uptime" => Ok(Inode::new(3, InodeKind::RegularFile)),
            "/meminfo" => Ok(Inode::new(4, InodeKind::RegularFile)),
            _ => Err(VfsError::NotFound),
        }
    }

    fn read(&self, inode: &Inode, offset: u64, buf: &mut [u8]) -> Result<usize, VfsError> {
        let content = match inode.inode_num {
            2 => Self::version_content(),
            3 => Self::uptime_content(),
            4 => Self::meminfo_content(),
            _ => return Err(VfsError::NotFound),
        };
        let start = offset as usize;
        if start >= content.len() {
            return Ok(0);
        }
        let available = &content[start..];
        let n = buf.len().min(available.len());
        buf[..n].copy_from_slice(&available[..n]);
        Ok(n)
    }

    fn write(&self, _inode: &Inode, _offset: u64, _buf: &[u8]) -> Result<usize, VfsError> {
        Err(VfsError::PermissionDenied)
    }

    fn readdir(&self, inode: &Inode) -> Result<Vec<DirEntry>, VfsError> {
        if inode.inode_num != 1 {
            return Err(VfsError::NotADirectory);
        }
        Ok(vec![
            DirEntry { name: String::from("version"), inode_num: 2, kind: InodeKind::RegularFile },
            DirEntry { name: String::from("uptime"),  inode_num: 3, kind: InodeKind::RegularFile },
            DirEntry { name: String::from("meminfo"), inode_num: 4, kind: InodeKind::RegularFile },
        ])
    }

    fn create(
        &self,
        _parent: &Inode,
        _name: &str,
        _kind: InodeKind,
        _mode: FileMode,
    ) -> Result<Inode, VfsError> {
        Err(VfsError::PermissionDenied)
    }

    fn unlink(&self, _parent: &Inode, _name: &str) -> Result<(), VfsError> {
        Err(VfsError::PermissionDenied)
    }
}
