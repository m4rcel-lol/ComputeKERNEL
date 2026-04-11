//! Virtual Filesystem (VFS) layer — inode and filesystem abstractions.

use alloc::{string::String, vec::Vec};

/// VFS error type.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum VfsError {
    NotFound,
    PermissionDenied,
    NotADirectory,
    IsADirectory,
    NoSpace,
    InvalidPath,
    NotSupported,
    Io,
}

/// Inode type.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum InodeKind {
    RegularFile,
    Directory,
    Symlink,
    CharDevice,
    BlockDevice,
    Pipe,
    Socket,
}

/// Unix-style permission bits.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FileMode(pub u16);

impl FileMode {
    pub const USER_RWX: u16 = 0o700;
    pub const GROUP_RWX: u16 = 0o070;
    pub const OTHER_RWX: u16 = 0o007;
    pub const DEFAULT_FILE: u16 = 0o644;
    pub const DEFAULT_DIR: u16 = 0o755;
}

/// An inode represents a file or directory within the VFS.
#[derive(Debug)]
pub struct Inode {
    pub inode_num: u64,
    pub kind: InodeKind,
    pub mode: FileMode,
    pub size: u64,
    pub uid: u32,
    pub gid: u32,
    pub link_count: u32,
    pub atime: u64,
    pub mtime: u64,
    pub ctime: u64,
}

impl Inode {
    /// Create a new inode with sensible defaults.
    pub fn new(inode_num: u64, kind: InodeKind) -> Self {
        let mode = match kind {
            InodeKind::Directory => FileMode(FileMode::DEFAULT_DIR),
            _ => FileMode(FileMode::DEFAULT_FILE),
        };
        Inode {
            inode_num,
            kind,
            mode,
            size: 0,
            uid: 0,
            gid: 0,
            link_count: 1,
            atime: 0,
            mtime: 0,
            ctime: 0,
        }
    }
}

/// A VFS directory entry.
#[derive(Debug, Clone)]
pub struct DirEntry {
    pub name: String,
    pub inode_num: u64,
    pub kind: InodeKind,
}

/// Trait implemented by all filesystems.
pub trait FileSystem {
    /// Return the filesystem name (e.g. "ext4", "proc").
    fn name(&self) -> &str;

    /// Look up an inode by path.
    fn lookup(&self, path: &str) -> Result<Inode, VfsError>;

    /// Read data from an inode at a given byte offset.
    fn read(&self, inode: &Inode, offset: u64, buf: &mut [u8]) -> Result<usize, VfsError>;

    /// Write data to an inode at a given byte offset.
    fn write(&self, inode: &Inode, offset: u64, buf: &[u8]) -> Result<usize, VfsError>;

    /// List directory entries.
    fn readdir(&self, inode: &Inode) -> Result<Vec<DirEntry>, VfsError>;

    /// Create a new file or directory.
    fn create(
        &self,
        parent: &Inode,
        name: &str,
        kind: InodeKind,
        mode: FileMode,
    ) -> Result<Inode, VfsError>;

    /// Remove an entry from a directory.
    fn unlink(&self, parent: &Inode, name: &str) -> Result<(), VfsError>;
}

/// Mount point entry in the global mount table.
pub struct MountPoint {
    pub path: String,
    pub fs: alloc::boxed::Box<dyn FileSystem + Send + Sync>,
}
