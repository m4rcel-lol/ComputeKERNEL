/*
 * kernel/vfs/vfs.c
 *
 * Minimal in-kernel Virtual Filesystem layer.
 *
 * Supports a single root mount.  Paths are always absolute ("/…").
 * No permissions, no timestamps – just enough to read/write ramfs files.
 */
#include <ck/vfs.h>
#include <ck/kernel.h>
#include <ck/mm.h>
#include <ck/string.h>
#include <ck/types.h>

static struct vfs_node *vfs_root = NULL;
static struct file_desc fdt[VFS_FD_MAX];

/* ── Path resolution ────────────────────────────────────────────────── */

/* Walk one component of a path in directory 'dir'.
   Returns NULL if not found. */
static struct vfs_node *dir_lookup(struct vfs_node *dir, const char *name, size_t len)
{
    if (!dir || dir->type != VFS_DIR) return NULL;
    struct vfs_node *child = dir->child;
    while (child) {
        if (strncmp(child->name, name, len) == 0 && child->name[len] == '\0')
            return child;
        child = child->sibling;
    }
    return NULL;
}

struct vfs_node *vfs_lookup(const char *path)
{
    if (!path || path[0] != '/')
        return NULL;

    struct vfs_node *cur = vfs_root;
    const char *p = path + 1; /* skip leading '/' */

    if (*p == '\0')
        return vfs_root;

    while (*p) {
        /* Find end of this component */
        const char *end = p;
        while (*end && *end != '/') end++;
        size_t len = (size_t)(end - p);

        cur = dir_lookup(cur, p, len);
        if (!cur) return NULL;

        p = end;
        if (*p == '/') p++;
    }
    return cur;
}

/* ── Subsystem init ─────────────────────────────────────────────────── */

void vfs_init(void)
{
    memset(fdt, 0, sizeof(fdt));
    vfs_root = ramfs_create_root();

    /* Create a few initial filesystem entries */
    struct vfs_node *dev = ramfs_create_dir(vfs_root, "dev");
    struct vfs_node *etc = ramfs_create_dir(vfs_root, "etc");
    struct vfs_node *tmp = ramfs_create_dir(vfs_root, "tmp");
    (void)dev; (void)tmp;

    const char *motd = "Welcome to ComputeKERNEL!\n"
                       "A production-ready 64-bit kernel.\n";
    ramfs_create_file(etc, "motd", motd, strlen(motd));

    const char *krel = "ComputeKERNEL 1.0.0\n";
    ramfs_create_file(etc, "kernel-release", krel, strlen(krel));

    ck_puts("[vfs] rootfs mounted (ramfs)\n");
}

int vfs_mount(const char *path, struct vfs_node *root)
{
    (void)path;
    vfs_root = root;
    return 0;
}

/* ── File descriptor layer ──────────────────────────────────────────── */

int vfs_open(const char *path, int flags)
{
    struct vfs_node *node = vfs_lookup(path);
    if (!node) {
        if (!(flags & O_CREAT))
            return -1;
        /* Create the file in parent dir */
        /* Find parent path */
        const char *last = path + strlen(path);
        while (last > path && *last != '/') last--;
        const char *name = (last == path) ? path + 1 : last + 1;

        char parent_path[VFS_NAME_MAX];
        size_t plen = (size_t)(last - path);
        if (plen == 0) {
            parent_path[0] = '/'; parent_path[1] = '\0';
        } else {
            strncpy(parent_path, path, plen);
            parent_path[plen] = '\0';
        }
        struct vfs_node *parent = vfs_lookup(parent_path);
        if (!parent || parent->type != VFS_DIR)
            return -1;
        node = ramfs_create_file(parent, name, NULL, 0);
        if (!node) return -1;
    }

    /* Allocate fd */
    for (int i = 0; i < VFS_FD_MAX; i++) {
        if (!fdt[i].used) {
            fdt[i].node   = node;
            fdt[i].flags  = flags;
            fdt[i].offset = (flags & O_APPEND) ? node->size : 0;
            fdt[i].used   = 1;
            if (node->ops && node->ops->open)
                node->ops->open(node, flags);
            return i;
        }
    }
    return -1; /* too many open files */
}

int vfs_close(int fd)
{
    if (fd < 0 || fd >= VFS_FD_MAX || !fdt[fd].used)
        return -1;
    if (fdt[fd].node->ops && fdt[fd].node->ops->close)
        fdt[fd].node->ops->close(fdt[fd].node);
    fdt[fd].used = 0;
    return 0;
}

ssize_t vfs_read(int fd, void *buf, size_t len)
{
    if (fd < 0 || fd >= VFS_FD_MAX || !fdt[fd].used)
        return -1;
    struct file_desc *f = &fdt[fd];
    if (!f->node->ops || !f->node->ops->read)
        return -1;
    ssize_t n = f->node->ops->read(f->node, buf, len, f->offset);
    if (n > 0) f->offset += (u64)n;
    return n;
}

ssize_t vfs_write(int fd, const void *buf, size_t len)
{
    if (fd < 0 || fd >= VFS_FD_MAX || !fdt[fd].used)
        return -1;
    struct file_desc *f = &fdt[fd];
    if (!f->node->ops || !f->node->ops->write)
        return -1;
    ssize_t n = f->node->ops->write(f->node, buf, len, f->offset);
    if (n > 0) f->offset += (u64)n;
    return n;
}

int vfs_readdir(int fd, u32 idx, char *name_out)
{
    if (fd < 0 || fd >= VFS_FD_MAX || !fdt[fd].used)
        return -1;
    struct file_desc *f = &fdt[fd];
    if (!f->node->ops || !f->node->ops->readdir)
        return -1;
    return f->node->ops->readdir(f->node, idx, name_out);
}
