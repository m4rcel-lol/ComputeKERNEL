#ifndef CK_VFS_H
#define CK_VFS_H

#include <ck/types.h>

#define VFS_NAME_MAX  256
#define VFS_FD_MAX    64

/* File-open flags */
#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_CREAT   0x0040
#define O_TRUNC   0x0200
#define O_APPEND  0x0400

/* Node types */
#define VFS_FILE 1
#define VFS_DIR  2

struct vfs_node;
struct file_ops {
    ssize_t (*read)(struct vfs_node *node, void *buf, size_t len, u64 off);
    ssize_t (*write)(struct vfs_node *node, const void *buf, size_t len, u64 off);
    int     (*readdir)(struct vfs_node *node, u32 idx, char *name_out);
    int     (*open)(struct vfs_node *node, int flags);
    int     (*close)(struct vfs_node *node);
    void    (*truncate)(struct vfs_node *node);    /* set file size to 0   */
    void    (*destroy)(struct vfs_node *node);     /* free fs-private data */
};

struct vfs_node {
    char            name[VFS_NAME_MAX];
    u32             type;
    u64             size;
    struct file_ops *ops;
    void            *data;          /* fs-private payload */
    struct vfs_node *parent;
    struct vfs_node *child;         /* first child (for dirs) */
    struct vfs_node *sibling;       /* next sibling */
};

/* File descriptor table entry */
struct file_desc {
    struct vfs_node *node;
    int              flags;
    u64              offset;
    int              used;
};

/* Subsystem init */
void vfs_init(void);

/* Mount a filesystem root at the given path */
int  vfs_mount(const char *path, struct vfs_node *root);

/* Standard file operations (return -1 on error) */
int    vfs_open(const char *path, int flags);
int    vfs_close(int fd);
ssize_t vfs_read(int fd, void *buf, size_t len);
ssize_t vfs_write(int fd, const void *buf, size_t len);
int    vfs_readdir(int fd, u32 idx, char *name_out);

/* Look up a node by absolute path; returns NULL if not found */
struct vfs_node *vfs_lookup(const char *path);

/* Create a directory at the given absolute path */
int vfs_mkdir(const char *path);

/* Remove a file or empty directory */
int vfs_unlink(const char *path);

/* Rename / move a node from src to dst absolute path */
int vfs_rename(const char *src, const char *dst);

/* ── RAM filesystem ─────────────────────────────────────────────────── */
struct vfs_node *ramfs_create_root(void);
struct vfs_node *ramfs_create_file(struct vfs_node *dir, const char *name,
                                   const void *data, size_t len);
struct vfs_node *ramfs_create_dir(struct vfs_node *dir, const char *name);

#endif /* CK_VFS_H */
