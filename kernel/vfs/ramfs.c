/*
 * kernel/vfs/ramfs.c
 *
 * RAM-based filesystem backend.
 *
 * File data is stored in a simple heap-allocated buffer.
 * Writes append to or overwrite the buffer (realloc-style).
 */
#include <ck/vfs.h>
#include <ck/mm.h>
#include <ck/kernel.h>
#include <ck/string.h>
#include <ck/types.h>

/* Per-file private data */
struct ramfs_file_data {
    u8    *buf;
    size_t cap;
};

/* ── File operations ────────────────────────────────────────────────── */

static ssize_t ramfs_read(struct vfs_node *node, void *buf, size_t len, u64 off)
{
    if (off >= node->size)
        return 0;
    size_t avail = (size_t)(node->size - off);
    size_t n = (len < avail) ? len : avail;
    struct ramfs_file_data *d = (struct ramfs_file_data *)node->data;
    memcpy(buf, d->buf + off, n);
    return (ssize_t)n;
}

static ssize_t ramfs_write(struct vfs_node *node, const void *buf, size_t len, u64 off)
{
    struct ramfs_file_data *d = (struct ramfs_file_data *)node->data;
    u64   end_off  = off + len;

    /* Grow buffer if necessary */
    if (end_off > d->cap) {
        size_t new_cap = (size_t)end_off * 2;
        u8 *new_buf = (u8 *)kmalloc(new_cap);
        if (!new_buf) return -1;
        if (d->buf) {
            memcpy(new_buf, d->buf, (size_t)node->size);
            kfree(d->buf);
        }
        d->buf = new_buf;
        d->cap = new_cap;
    }
    memcpy(d->buf + off, buf, len);
    if (end_off > node->size)
        node->size = end_off;
    return (ssize_t)len;
}

static int ramfs_readdir(struct vfs_node *node, u32 idx, char *name_out)
{
    if (node->type != VFS_DIR) return -1;
    struct vfs_node *child = node->child;
    u32 i = 0;
    while (child) {
        if (i == idx) {
            strncpy(name_out, child->name, VFS_NAME_MAX - 1);
            name_out[VFS_NAME_MAX - 1] = '\0';
            return 0;
        }
        child = child->sibling;
        i++;
    }
    return -1; /* end of directory */
}

static void ramfs_file_truncate(struct vfs_node *node)
{
    struct ramfs_file_data *d = (struct ramfs_file_data *)node->data;
    node->size = 0;
    if (d->buf && d->cap > 0)
        memset(d->buf, 0, d->cap);
}

static void ramfs_file_destroy(struct vfs_node *node)
{
    struct ramfs_file_data *d = (struct ramfs_file_data *)node->data;
    if (d->buf) kfree(d->buf);
    kfree(d);
    node->data = NULL;
}

static void ramfs_dir_destroy(struct vfs_node *node)
{
    /* Directories carry no fs-private heap data; nothing to free. */
    (void)node;
}

static struct file_ops ramfs_file_ops = {
    .read     = ramfs_read,
    .write    = ramfs_write,
    .readdir  = NULL,
    .open     = NULL,
    .close    = NULL,
    .truncate = ramfs_file_truncate,
    .destroy  = ramfs_file_destroy,
};

static struct file_ops ramfs_dir_ops = {
    .read     = NULL,
    .write    = NULL,
    .readdir  = ramfs_readdir,
    .open     = NULL,
    .close    = NULL,
    .truncate = NULL,
    .destroy  = ramfs_dir_destroy,
};

/* ── Node factory helpers ───────────────────────────────────────────── */

static struct vfs_node *alloc_node(const char *name, u32 type)
{
    struct vfs_node *n = (struct vfs_node *)kzalloc(sizeof(*n));
    if (!n) return NULL;
    strncpy(n->name, name, VFS_NAME_MAX - 1);
    n->type = type;
    return n;
}

static void dir_add_child(struct vfs_node *dir, struct vfs_node *child)
{
    child->parent  = dir;
    child->sibling = dir->child;
    dir->child     = child;
}

struct vfs_node *ramfs_create_root(void)
{
    struct vfs_node *root = alloc_node("/", VFS_DIR);
    if (!root) return NULL;
    root->ops = &ramfs_dir_ops;
    return root;
}

struct vfs_node *ramfs_create_dir(struct vfs_node *parent, const char *name)
{
    struct vfs_node *dir = alloc_node(name, VFS_DIR);
    if (!dir) return NULL;
    dir->ops = &ramfs_dir_ops;
    if (parent) dir_add_child(parent, dir);
    return dir;
}

struct vfs_node *ramfs_create_file(struct vfs_node *parent, const char *name,
                                   const void *data, size_t len)
{
    struct vfs_node *f = alloc_node(name, VFS_FILE);
    if (!f) return NULL;

    struct ramfs_file_data *fd = (struct ramfs_file_data *)kzalloc(sizeof(*fd));
    if (!fd) { kfree(f); return NULL; }

    if (len > 0 && data) {
        fd->buf = (u8 *)kmalloc(len);
        if (!fd->buf) { kfree(fd); kfree(f); return NULL; }
        memcpy(fd->buf, data, len);
        fd->cap = len;
        f->size = len;
    }

    f->data = fd;
    f->ops  = &ramfs_file_ops;
    if (parent) dir_add_child(parent, f);
    return f;
}
