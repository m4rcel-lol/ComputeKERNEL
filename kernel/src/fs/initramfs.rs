//! initramfs (cpio newc format) parser.
//!
//! Parses a cpio archive embedded in the bootloader ramdisk and exposes it
//! as a read-only in-memory filesystem.

use crate::serial_println;
use alloc::{string::{String, ToString}, vec::Vec};

use super::vfs::{DirEntry, FileMode, FileSystem, Inode, InodeKind, VfsError};

/// A single file extracted from the cpio archive.
#[derive(Debug, Clone)]
pub struct CpioEntry {
    pub path: String,
    pub inode_num: u64,
    pub mode: u32,
    pub size: u64,
    /// Byte offset of this file's data in the original archive.
    pub data_offset: usize,
}

/// In-memory initramfs backed by a cpio archive.
pub struct InitramFs {
    data: &'static [u8],
    entries: Vec<CpioEntry>,
}

const CPIO_NEWC_MAGIC: &[u8; 6] = b"070701";
const HEADER_SIZE: usize = 110;

impl InitramFs {
    /// Parse a cpio newc archive and return an InitramFs.
    ///
    /// `data` must remain valid for the kernel's lifetime.
    pub fn new(data: &'static [u8]) -> Option<Self> {
        let mut entries = Vec::new();
        let mut offset = 0usize;
        let mut inode_num = 2u64;

        while offset + HEADER_SIZE <= data.len() {
            if &data[offset..offset + 6] != CPIO_NEWC_MAGIC {
                break;
            }

            // Parse newc header fields (all ASCII hex, 8 chars each).
            let namesize = parse_hex8(&data[offset + 94..offset + 102])?;
            let filesize = parse_hex8(&data[offset + 54..offset + 62])? as u64;
            let mode = parse_hex8(&data[offset + 14..offset + 22])?;

            let name_offset = offset + HEADER_SIZE;
            let name_end = name_offset + namesize as usize;
            if name_end > data.len() {
                break;
            }

            let name_bytes = &data[name_offset..name_end - 1]; // strip NUL
            let path = core::str::from_utf8(name_bytes).ok()?;

            // Align data to 4-byte boundary after header + name.
            let data_start = align4(name_end);
            let data_end = data_start + filesize as usize;

            if path == "TRAILER!!!" {
                break;
            }

            entries.push(CpioEntry {
                path: String::from(path),
                inode_num,
                mode,
                size: filesize,
                data_offset: data_start,
            });
            inode_num += 1;

            offset = align4(data_end);
        }

        serial_println!("[initramfs] Parsed {} entries", entries.len());
        Some(InitramFs { data, entries })
    }

    fn find(&self, path: &str) -> Option<&CpioEntry> {
        self.entries.iter().find(|e| e.path == path || e.path == path.trim_start_matches('/'))
    }
}

fn parse_hex8(buf: &[u8]) -> Option<u32> {
    let s = core::str::from_utf8(buf).ok()?;
    u32::from_str_radix(s, 16).ok()
}

fn align4(x: usize) -> usize {
    (x + 3) & !3
}

impl FileSystem for InitramFs {
    fn name(&self) -> &str {
        "initramfs"
    }

    fn lookup(&self, path: &str) -> Result<Inode, VfsError> {
        if path == "/" || path.is_empty() {
            return Ok(Inode::new(1, InodeKind::Directory));
        }
        let entry = self.find(path).ok_or(VfsError::NotFound)?;
        let kind = if entry.mode & 0o170000 == 0o040000 {
            InodeKind::Directory
        } else if entry.mode & 0o170000 == 0o120000 {
            InodeKind::Symlink
        } else {
            InodeKind::RegularFile
        };
        let mut inode = Inode::new(entry.inode_num, kind);
        inode.size = entry.size;
        inode.mode = FileMode(entry.mode as u16);
        Ok(inode)
    }

    fn read(&self, inode: &Inode, offset: u64, buf: &mut [u8]) -> Result<usize, VfsError> {
        let entry = self
            .entries
            .iter()
            .find(|e| e.inode_num == inode.inode_num)
            .ok_or(VfsError::NotFound)?;

        let start = (entry.data_offset as u64 + offset) as usize;
        let end = (entry.data_offset as u64 + entry.size) as usize;
        if start >= end {
            return Ok(0);
        }
        let available = &self.data[start..end];
        let n = buf.len().min(available.len());
        buf[..n].copy_from_slice(&available[..n]);
        Ok(n)
    }

    fn write(&self, _inode: &Inode, _offset: u64, _buf: &[u8]) -> Result<usize, VfsError> {
        Err(VfsError::PermissionDenied)
    }

    fn readdir(&self, inode: &Inode) -> Result<Vec<DirEntry>, VfsError> {
        let prefix = if inode.inode_num == 1 {
            String::new()
        } else {
            let entry = self
                .entries
                .iter()
                .find(|e| e.inode_num == inode.inode_num)
                .ok_or(VfsError::NotFound)?;
            entry.path.clone()
        };

        let mut result = Vec::new();
        for entry in &self.entries {
            if entry.path.starts_with(prefix.as_str())
                && entry.path[prefix.len()..].trim_start_matches('/').contains('/')
                    == false
            {
                let name = entry.path[prefix.len()..]
                    .trim_start_matches('/')
                    .to_string();
                if !name.is_empty() {
                    let kind = if entry.mode & 0o170000 == 0o040000 {
                        InodeKind::Directory
                    } else {
                        InodeKind::RegularFile
                    };
                    result.push(DirEntry {
                        name,
                        inode_num: entry.inode_num,
                        kind,
                    });
                }
            }
        }
        Ok(result)
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
