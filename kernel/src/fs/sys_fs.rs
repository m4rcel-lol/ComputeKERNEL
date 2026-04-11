//! /sys filesystem — exposes kernel objects and hardware topology.

use alloc::{string::String, vec, vec::Vec};

use super::vfs::{DirEntry, FileMode, FileSystem, Inode, InodeKind, VfsError};

/// In-memory /sys filesystem (stub).
pub struct SysFs;

impl SysFs {
    pub fn new() -> Self {
        SysFs
    }
}

impl FileSystem for SysFs {
    fn name(&self) -> &str {
        "sysfs"
    }

    fn lookup(&self, path: &str) -> Result<Inode, VfsError> {
        match path {
            "/" | "" => Ok(Inode::new(1, InodeKind::Directory)),
            "/kernel" => Ok(Inode::new(2, InodeKind::Directory)),
            "/kernel/hostname" => Ok(Inode::new(3, InodeKind::RegularFile)),
            _ => Err(VfsError::NotFound),
        }
    }

    fn read(&self, inode: &Inode, offset: u64, buf: &mut [u8]) -> Result<usize, VfsError> {
        if offset > 0 {
            return Ok(0);
        }
        let content: &[u8] = match inode.inode_num {
            3 => b"computekernel\n",
            _ => return Err(VfsError::NotFound),
        };
        let n = buf.len().min(content.len());
        buf[..n].copy_from_slice(&content[..n]);
        Ok(n)
    }

    fn write(&self, _inode: &Inode, _offset: u64, _buf: &[u8]) -> Result<usize, VfsError> {
        Err(VfsError::PermissionDenied)
    }

    fn readdir(&self, inode: &Inode) -> Result<Vec<DirEntry>, VfsError> {
        match inode.inode_num {
            1 => Ok(vec![DirEntry {
                name: String::from("kernel"),
                inode_num: 2,
                kind: InodeKind::Directory,
            }]),
            2 => Ok(vec![DirEntry {
                name: String::from("hostname"),
                inode_num: 3,
                kind: InodeKind::RegularFile,
            }]),
            _ => Err(VfsError::NotADirectory),
        }
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
