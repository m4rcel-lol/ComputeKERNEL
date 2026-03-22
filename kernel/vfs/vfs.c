/*
 * kernel/vfs/vfs.c
 *
 * Minimal in-kernel Virtual Filesystem layer.
 *
 * Supports a single root mount.  Paths are always absolute ("/…").
 * No permissions, no timestamps – just enough to read/write ramfs files.
 *
 * Locking: a single spinlock protects the VFS node tree and the file
 * descriptor table.  All public functions acquire it on entry.
 */
#include <ck/vfs.h>
#include <ck/kernel.h>
#include <ck/mm.h>
#include <ck/string.h>
#include <ck/types.h>
#include <ck/spinlock.h>

static struct vfs_node *vfs_root = NULL;
static struct file_desc fdt[VFS_FD_MAX];
static spinlock_t       vfs_lock = SPINLOCK_INIT;

/* ── Path resolution ────────────────────────────────────────────────── */

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

/* Internal lookup – caller must hold vfs_lock */
static struct vfs_node *vfs_lookup_locked(const char *path)
{
    if (!path || path[0] != '/')
        return NULL;

    struct vfs_node *cur = vfs_root;
    const char *p = path + 1;

    if (*p == '\0')
        return vfs_root;

    while (*p) {
        const char *end = p;
        while (*end && *end != '/') end++;
        size_t len = (size_t)(end - p);
        if (len == 0) { p = end + 1; continue; }

        cur = dir_lookup(cur, p, len);
        if (!cur) return NULL;

        p = end;
        if (*p == '/') p++;
    }
    return cur;
}

struct vfs_node *vfs_lookup(const char *path)
{
    u64 flags = spin_lock_irqsave(&vfs_lock);
    struct vfs_node *n = vfs_lookup_locked(path);
    spin_unlock_irqrestore(&vfs_lock, flags);
    return n;
}

/* ── Subsystem init ─────────────────────────────────────────────────── */

void vfs_init(void)
{
    spin_lock_init(&vfs_lock);
    memset(fdt, 0, sizeof(fdt));
    vfs_root = ramfs_create_root();

    struct vfs_node *dev = ramfs_create_dir(vfs_root, "dev");
    struct vfs_node *etc = ramfs_create_dir(vfs_root, "etc");
    struct vfs_node *tmp = ramfs_create_dir(vfs_root, "tmp");
    struct vfs_node *var = ramfs_create_dir(vfs_root, "var");
    struct vfs_node *home = ramfs_create_dir(vfs_root, "home");
    (void)dev; (void)tmp; (void)var; (void)home;

    const char *motd = "Welcome to ComputeKERNEL!\n"
                       "Type 'help' for a list of commands.\n";
    ramfs_create_file(etc, "motd", motd, strlen(motd));

    const char *krel = "ComputeKERNEL 1.0.0\n";
    ramfs_create_file(etc, "kernel-release", krel, strlen(krel));

    const char *hostname = "computekernel\n";
    ramfs_create_file(etc, "hostname", hostname, strlen(hostname));

    ck_puts("[vfs] rootfs mounted (ramfs)\n");
}

int vfs_mount(const char *path, struct vfs_node *root)
{
    (void)path;
    u64 flags = spin_lock_irqsave(&vfs_lock);
    vfs_root = root;
    spin_unlock_irqrestore(&vfs_lock, flags);
    return 0;
}

/* ── File descriptor layer ──────────────────────────────────────────── */

int vfs_open(const char *path, int flags)
{
    u64 lflags = spin_lock_irqsave(&vfs_lock);

    struct vfs_node *node = vfs_lookup_locked(path);
    if (!node) {
        if (!(flags & O_CREAT)) {
            spin_unlock_irqrestore(&vfs_lock, lflags);
            return -1;
        }
        /* Create the file in its parent directory */
        const char *last = path + strlen(path);
        while (last > path && *last != '/') last--;
        const char *name = (last == path) ? path + 1 : last + 1;

        char parent_path[VFS_NAME_MAX];
        size_t plen = (size_t)(last - path);
        if (plen == 0) {
            parent_path[0] = '/'; parent_path[1] = '\0';
        } else {
            if (plen >= VFS_NAME_MAX) {
                spin_unlock_irqrestore(&vfs_lock, lflags);
                return -1;
            }
            strncpy(parent_path, path, plen);
            parent_path[plen] = '\0';
        }
        struct vfs_node *parent = vfs_lookup_locked(parent_path);
        if (!parent || parent->type != VFS_DIR) {
            spin_unlock_irqrestore(&vfs_lock, lflags);
            return -1;
        }
        node = ramfs_create_file(parent, name, NULL, 0);
        if (!node) {
            spin_unlock_irqrestore(&vfs_lock, lflags);
            return -1;
        }
    }

    /* Allocate a file descriptor */
    for (int i = 0; i < VFS_FD_MAX; i++) {
        if (!fdt[i].used) {
            if ((flags & O_TRUNC) && node->type == VFS_FILE) {
                if (node->ops && node->ops->truncate)
                    node->ops->truncate(node);
            }
            fdt[i].node   = node;
            fdt[i].flags  = flags;
            fdt[i].offset = (flags & O_APPEND) ? node->size : 0;
            fdt[i].used   = 1;
            if (node->ops && node->ops->open)
                node->ops->open(node, flags);
            spin_unlock_irqrestore(&vfs_lock, lflags);
            return i;
        }
    }
    spin_unlock_irqrestore(&vfs_lock, lflags);
    return -1;
}

int vfs_close(int fd)
{
    if (fd < 0 || fd >= VFS_FD_MAX)
        return -1;
    u64 flags = spin_lock_irqsave(&vfs_lock);
    if (!fdt[fd].used) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return -1;
    }
    if (fdt[fd].node->ops && fdt[fd].node->ops->close)
        fdt[fd].node->ops->close(fdt[fd].node);
    fdt[fd].used = 0;
    spin_unlock_irqrestore(&vfs_lock, flags);
    return 0;
}

ssize_t vfs_read(int fd, void *buf, size_t len)
{
    if (fd < 0 || fd >= VFS_FD_MAX)
        return -1;
    u64 flags = spin_lock_irqsave(&vfs_lock);
    if (!fdt[fd].used) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return -1;
    }
    struct file_desc *f = &fdt[fd];
    if (!f->node->ops || !f->node->ops->read) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return -1;
    }
    ssize_t n = f->node->ops->read(f->node, buf, len, f->offset);
    if (n > 0) f->offset += (u64)n;
    spin_unlock_irqrestore(&vfs_lock, flags);
    return n;
}

ssize_t vfs_write(int fd, const void *buf, size_t len)
{
    if (fd < 0 || fd >= VFS_FD_MAX)
        return -1;
    u64 flags = spin_lock_irqsave(&vfs_lock);
    if (!fdt[fd].used) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return -1;
    }
    struct file_desc *f = &fdt[fd];
    if (!f->node->ops || !f->node->ops->write) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return -1;
    }
    ssize_t n = f->node->ops->write(f->node, buf, len, f->offset);
    if (n > 0) f->offset += (u64)n;
    spin_unlock_irqrestore(&vfs_lock, flags);
    return n;
}

int vfs_readdir(int fd, u32 idx, char *name_out)
{
    if (fd < 0 || fd >= VFS_FD_MAX)
        return -1;
    u64 flags = spin_lock_irqsave(&vfs_lock);
    if (!fdt[fd].used) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return -1;
    }
    struct file_desc *f = &fdt[fd];
    if (!f->node->ops || !f->node->ops->readdir) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return -1;
    }
    int r = f->node->ops->readdir(f->node, idx, name_out);
    spin_unlock_irqrestore(&vfs_lock, flags);
    return r;
}

/* ── Path helper ────────────────────────────────────────────────────── */

static const char *split_parent(const char *path, char *parent_path)
{
    if (!path || path[0] != '/')
        return NULL;

    const char *last = path + strlen(path);
    while (last > path && *last != '/') last--;

    const char *name = last + 1;
    if (*name == '\0')
        return NULL;

    size_t plen = (size_t)(last - path);
    if (plen == 0) {
        parent_path[0] = '/'; parent_path[1] = '\0';
    } else {
        if (plen >= VFS_NAME_MAX)
            return NULL;
        strncpy(parent_path, path, plen);
        parent_path[plen] = '\0';
    }
    return name;
}

/* ── mkdir / unlink / rename ────────────────────────────────────────── */

int vfs_mkdir(const char *path)
{
    char parent_path[VFS_NAME_MAX];
    const char *name = split_parent(path, parent_path);
    if (!name) return -1;

    u64 flags = spin_lock_irqsave(&vfs_lock);

    if (vfs_lookup_locked(path)) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return -1;  /* already exists */
    }

    struct vfs_node *parent = vfs_lookup_locked(parent_path);
    if (!parent || parent->type != VFS_DIR) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return -1;
    }

    struct vfs_node *dir = ramfs_create_dir(parent, name);
    spin_unlock_irqrestore(&vfs_lock, flags);
    return dir ? 0 : -1;
}

int vfs_unlink(const char *path)
{
    u64 flags = spin_lock_irqsave(&vfs_lock);

    struct vfs_node *node = vfs_lookup_locked(path);
    if (!node || node == vfs_root) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return -1;
    }
    if (node->type == VFS_DIR && node->child) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return -1;  /* directory not empty */
    }

    struct vfs_node *parent = node->parent;
    if (!parent) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return -1;
    }

    /* Unlink from parent's child list */
    if (parent->child == node) {
        parent->child = node->sibling;
    } else {
        struct vfs_node *prev = parent->child;
        while (prev && prev->sibling != node) prev = prev->sibling;
        if (!prev) {
            spin_unlock_irqrestore(&vfs_lock, flags);
            return -1;
        }
        prev->sibling = node->sibling;
    }

    /* Free fs-private data */
    if (node->ops && node->ops->destroy)
        node->ops->destroy(node);
    kfree(node);

    spin_unlock_irqrestore(&vfs_lock, flags);
    return 0;
}

int vfs_rename(const char *src, const char *dst)
{
    u64 flags = spin_lock_irqsave(&vfs_lock);

    struct vfs_node *node = vfs_lookup_locked(src);
    if (!node || node == vfs_root) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return -1;
    }
    if (vfs_lookup_locked(dst)) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return -1;  /* destination already exists */
    }

    char dst_parent_path[VFS_NAME_MAX];
    const char *new_name = split_parent(dst, dst_parent_path);
    if (!new_name) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return -1;
    }

    struct vfs_node *new_parent = vfs_lookup_locked(dst_parent_path);
    if (!new_parent || new_parent->type != VFS_DIR) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return -1;
    }

    /* Remove from old parent */
    struct vfs_node *old_parent = node->parent;
    if (!old_parent) {
        spin_unlock_irqrestore(&vfs_lock, flags);
        return -1;
    }
    if (old_parent->child == node) {
        old_parent->child = node->sibling;
    } else {
        struct vfs_node *prev = old_parent->child;
        while (prev && prev->sibling != node) prev = prev->sibling;
        if (!prev) {
            spin_unlock_irqrestore(&vfs_lock, flags);
            return -1;
        }
        prev->sibling = node->sibling;
    }

    /* Update name and attach to new parent */
    strncpy(node->name, new_name, VFS_NAME_MAX - 1);
    node->name[VFS_NAME_MAX - 1] = '\0';
    node->sibling     = new_parent->child;
    new_parent->child = node;
    node->parent      = new_parent;

    spin_unlock_irqrestore(&vfs_lock, flags);
    return 0;
}
