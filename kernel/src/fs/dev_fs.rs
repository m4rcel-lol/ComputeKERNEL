//! /dev filesystem — exposes character and block device nodes.

use alloc::{string::String, vec, vec::Vec};

use super::vfs::{DirEntry, FileMode, FileSystem, Inode, InodeKind, VfsError};

/// In-memory /dev filesystem.
pub struct DevFs;

impl DevFs {
    pub fn new() -> Self {
        DevFs
    }
}

/// Null device — discards all writes, reads return EOF.
pub struct NullDevice;
/// Zero device — reads return zeros, discards writes.
pub struct ZeroDevice;
/// Full device — writes fail with ENOSPC, reads return zeros.
pub struct FullDevice;

impl FileSystem for DevFs {
    fn name(&self) -> &str {
        "devfs"
    }

    fn lookup(&self, path: &str) -> Result<Inode, VfsError> {
        match path {
            "/" | "" => Ok(Inode::new(1, InodeKind::Directory)),
            "/null" => {
                let mut inode = Inode::new(2, InodeKind::CharDevice);
                inode.mode = FileMode(0o666);
                Ok(inode)
            }
            "/zero" => {
                let mut inode = Inode::new(3, InodeKind::CharDevice);
                inode.mode = FileMode(0o666);
                Ok(inode)
            }
            "/full" => {
                let mut inode = Inode::new(4, InodeKind::CharDevice);
                inode.mode = FileMode(0o666);
                Ok(inode)
            }
            "/ttyS0" => {
                let mut inode = Inode::new(5, InodeKind::CharDevice);
                inode.mode = FileMode(0o660);
                Ok(inode)
            }
            _ => Err(VfsError::NotFound),
        }
    }

    fn read(&self, inode: &Inode, _offset: u64, buf: &mut [u8]) -> Result<usize, VfsError> {
        match inode.inode_num {
            2 => Ok(0),                              // /dev/null → EOF
            3 | 4 => { buf.fill(0); Ok(buf.len()) } // /dev/zero, /dev/full
            5 => Ok(0),                              // /dev/ttyS0 stub
            _ => Err(VfsError::NotFound),
        }
    }

    fn write(&self, inode: &Inode, _offset: u64, buf: &[u8]) -> Result<usize, VfsError> {
        match inode.inode_num {
            2 => Ok(buf.len()),  // /dev/null → discard
            3 => Ok(buf.len()),  // /dev/zero → discard
            4 => Err(VfsError::NoSpace), // /dev/full → ENOSPC
            5 => {
                // /dev/ttyS0 → forward to serial port
                for &byte in buf {
                    crate::drivers::serial::_serial_print(format_args!("{}", byte as char));
                }
                Ok(buf.len())
            }
            _ => Err(VfsError::NotFound),
        }
    }

    fn readdir(&self, inode: &Inode) -> Result<Vec<DirEntry>, VfsError> {
        if inode.inode_num != 1 {
            return Err(VfsError::NotADirectory);
        }
        Ok(vec![
            DirEntry { name: String::from("null"),  inode_num: 2, kind: InodeKind::CharDevice },
            DirEntry { name: String::from("zero"),  inode_num: 3, kind: InodeKind::CharDevice },
            DirEntry { name: String::from("full"),  inode_num: 4, kind: InodeKind::CharDevice },
            DirEntry { name: String::from("ttyS0"), inode_num: 5, kind: InodeKind::CharDevice },
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
