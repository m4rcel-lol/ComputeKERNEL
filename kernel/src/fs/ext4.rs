//! ext4 filesystem driver stub.
//!
//! Implements the VFS `FileSystem` trait over a block device.
//! Full journaling and extent-tree support is future work.

use crate::serial_println;
use alloc::{string::String, vec::Vec};

use super::vfs::{DirEntry, FileMode, FileSystem, Inode, InodeKind, VfsError};

/// Stub ext4 filesystem.
pub struct Ext4Fs {
    /// Device path (e.g. "/dev/sda1").
    device: String,
}

impl Ext4Fs {
    /// Mount an ext4 filesystem from the given block device.
    pub fn new(device: &str) -> Self {
        serial_println!("[ext4] mounting {} (stub)", device);
        Ext4Fs {
            device: String::from(device),
        }
    }
}

impl FileSystem for Ext4Fs {
    fn name(&self) -> &str {
        "ext4"
    }

    fn lookup(&self, _path: &str) -> Result<Inode, VfsError> {
        Err(VfsError::NotSupported)
    }

    fn read(&self, _inode: &Inode, _offset: u64, _buf: &mut [u8]) -> Result<usize, VfsError> {
        Err(VfsError::NotSupported)
    }

    fn write(&self, _inode: &Inode, _offset: u64, _buf: &[u8]) -> Result<usize, VfsError> {
        Err(VfsError::NotSupported)
    }

    fn readdir(&self, _inode: &Inode) -> Result<Vec<DirEntry>, VfsError> {
        Err(VfsError::NotSupported)
    }

    fn create(
        &self,
        _parent: &Inode,
        _name: &str,
        _kind: InodeKind,
        _mode: FileMode,
    ) -> Result<Inode, VfsError> {
        Err(VfsError::NotSupported)
    }

    fn unlink(&self, _parent: &Inode, _name: &str) -> Result<(), VfsError> {
        Err(VfsError::NotSupported)
    }
}
