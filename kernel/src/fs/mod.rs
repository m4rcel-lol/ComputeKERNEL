//! Virtual Filesystem (VFS) layer.
//!
//! Provides filesystem-independent abstractions used by all kernel filesystems.

pub mod vfs;
pub mod ext4;
pub mod proc_fs;
pub mod sys_fs;
pub mod dev_fs;
pub mod initramfs;

pub use vfs::{FileSystem, Inode, InodeKind, VfsError};
