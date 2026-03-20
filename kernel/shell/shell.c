/*
 * kernel/shell/shell.c
 *
 * cksh – ComputeKERNEL interactive shell.
 *
 * Runs as a kernel task; reads PS/2 keyboard input, echoes characters,
 * and dispatches built-in commands.
 */
#include <ck/kernel.h>
#include <ck/io.h>
#include <ck/types.h>
#include <ck/string.h>
#include <ck/vfs.h>
#include <ck/sched.h>
#include <ck/mm.h>
#include <ck/keyboard.h>
#include <ck/mouse.h>

/* Provided by pit.c / keyboard.c */
extern u64  pit_get_ticks(void);
extern void pit_sleep_ms(u64 ms);
extern u64  pmm_free_pages(void);

#define SHELL_LINE_MAX  256
#define SHELL_HOSTNAME  "computekernel"
#define SHELL_USER      "root"

/* Printable ASCII range */
#define ASCII_SPACE 0x20
#define ASCII_DEL   0x7f

/* Special key codes (must match keyboard.c definitions) */
#define KEY_UP    0x10
#define KEY_DOWN  0x11
#define KEY_LEFT  0x12
#define KEY_RIGHT 0x13
#define KEY_PGUP  0x14
#define KEY_PGDN  0x15
#define KEY_CTRL_C 0x03
#define KEY_CTRL_X 0x18
#define KEY_CTRL_V 0x16

/* PIT runs at 100 Hz */
#define PIT_HZ 100UL

/* ── Keyboard controller constants (for reboot) ─────────────────────── */
#define KBD_STATUS_PORT 0x64
#define KBD_CMD_PORT    0x64
#define KBD_STATUS_IBF  0x02   /* Input buffer full – wait before writing */
#define KBD_CMD_RESET   0xFE   /* Pulse reset line */

/* ── Command-history ring buffer ─────────────────────────────────────── */
#define SHELL_HIST_MAX  16
#define SHELL_HIST_LEN  SHELL_LINE_MAX
#define SHELL_SCROLL_TO_TOP_LINES 1000U
#define PATH_SEG_MAX 128
#define SHELL_FS_RECURSE_MAX 32

static char shell_hist[SHELL_HIST_MAX][SHELL_HIST_LEN];
static int  shell_hist_count = 0;
static int  shell_hist_head  = 0;   /* next write slot */

/* ── History navigation state ────────────────────────────────────────── */
/* -1 means not currently navigating; 0 = most recent entry, 1 = next older, … */
static int  shell_hist_nav  = -1;
static char shell_line_saved[SHELL_LINE_MAX]; /* line saved before nav started */

/* ── Current working directory ───────────────────────────────────────── */
static char shell_cwd[VFS_NAME_MAX] = "/";
static char shell_clipboard[SHELL_LINE_MAX];

static void shell_exec(char *line);
static void cmd_installer_style(void);
static void shell_print_banner(void);

static int parse_u32_arg(const char *s, u32 *out)
{
    if (out)
        *out = 0;
    if (!s || !*s || !out)
        return -1;
    u32 value = 0;
    while (*s) {
        if (*s < '0' || *s > '9')
            return -1;
        u32 digit = (u32)(*s - '0');
        if (value > (0xFFFFFFFFU - digit) / 10U)
            return -1;
        value = value * 10U + digit;
        s++;
    }
    *out = value;
    return 0;
}

/* ── History helpers ─────────────────────────────────────────────────── */

static void hist_add(const char *line)
{
    if (!line || !*line) return;
    strncpy(shell_hist[shell_hist_head], line, SHELL_HIST_LEN - 1);
    shell_hist[shell_hist_head][SHELL_HIST_LEN - 1] = '\0';
    shell_hist_head = (shell_hist_head + 1) % SHELL_HIST_MAX;
    if (shell_hist_count < SHELL_HIST_MAX)
        shell_hist_count++;
}

/* Return the history entry at nav offset (0 = most recent). NULL if out of range. */
static const char *hist_get(int offset)
{
    if (offset < 0 || offset >= shell_hist_count)
        return NULL;
    /*
     * Multiply by 2 before adding the offset so the intermediate value is
     * always positive even when shell_hist_head == 0, avoiding a negative
     * result before the final modulo.
     */
    int idx = (shell_hist_head - 1 - offset + SHELL_HIST_MAX * 2) % SHELL_HIST_MAX;
    return shell_hist[idx];
}

/* ── Path helper ─────────────────────────────────────────────────────── */

static int path_normalize_abs(const char *in, char *out)
{
    if (!in || in[0] != '/')
        return -1;

    size_t in_len = strlen(in);
    if (in_len >= VFS_NAME_MAX)
        return -1;

    char tmp[VFS_NAME_MAX];
    memcpy(tmp, in, in_len + 1);

    int seg_start[PATH_SEG_MAX];
    int seg_count = 0;
    size_t out_len = 1;
    out[0] = '/';
    out[1] = '\0';

    size_t i = 1;
    while (tmp[i]) {
        while (tmp[i] == '/')
            i++;
        if (!tmp[i])
            break;

        size_t start = i;
        while (tmp[i] && tmp[i] != '/')
            i++;
        size_t len = i - start;

        if (len == 1 && tmp[start] == '.')
            continue;
        if (len == 2 && tmp[start] == '.' && tmp[start + 1] == '.') {
            if (seg_count > 0) {
                out_len = (size_t)seg_start[--seg_count];
                out[out_len] = '\0';
            }
            continue;
        }

        if (out_len > 1) {
            if (out_len + 1 >= VFS_NAME_MAX)
                return -1;
            out[out_len++] = '/';
        }

        if (out_len + len >= VFS_NAME_MAX)
            return -1;
        if (seg_count >= PATH_SEG_MAX)
            return -1;
        seg_start[seg_count++] = (int)(out_len > 1 ? out_len - 1 : 1);
        memcpy(out + out_len, tmp + start, len);
        out_len += len;
        out[out_len] = '\0';
    }

    return 0;
}

/* Resolve 'arg' relative to shell_cwd into 'out' (VFS_NAME_MAX bytes). */
static int path_resolve(const char *arg, char *out)
{
    char combined[VFS_NAME_MAX];

    if (!arg || !*arg) {
        return path_normalize_abs(shell_cwd, out);
    }
    if (arg[0] == '/') {
        return path_normalize_abs(arg, out);
    }
    /* Relative path: prepend CWD */
    size_t cwdlen = strlen(shell_cwd);
    size_t arglen = strlen(arg);
    if (cwdlen + 1 + arglen >= VFS_NAME_MAX) {
        return -1;
    }
    memcpy(combined, shell_cwd, cwdlen);
    if (combined[cwdlen - 1] != '/') {
        combined[cwdlen++] = '/';
    }
    memcpy(combined + cwdlen, arg, arglen + 1);
    return path_normalize_abs(combined, out);
}

static int path_resolve_or_error(const char *cmd, const char *arg, char *out)
{
    if (path_resolve(arg, out) == 0)
        return 0;
    ck_printk("%s: %s: path is too long\n", cmd, (arg && *arg) ? arg : "/");
    return -1;
}

/* ── Built-in commands ───────────────────────────────────────────────── */

static void cmd_help(void)
{
    ck_set_color(CK_COLOR_LIGHT_CYAN);
    ck_puts("+------------------ cksh built-in commands ------------------+\n");
    ck_reset_color();

    ck_set_color(CK_COLOR_YELLOW);
    ck_puts("[ CORE ]\n");
    ck_reset_color();
    ck_puts("  help                  show this help message\n");
    ck_puts("  clear                 clear the screen\n");
    ck_puts("  echo [text]           print text to the screen\n");

    ck_set_color(CK_COLOR_YELLOW);
    ck_puts("[ SYSTEM INFO ]\n");
    ck_reset_color();
    ck_puts("  uname                 print kernel/OS information\n");
    ck_puts("  fastfetch             display system information\n");
    ck_puts("  neofetch              alias for fastfetch\n");
    ck_puts("  free                  show free memory pages\n");
    ck_puts("  mem                   show detailed memory statistics\n");
    ck_puts("  motd                  show session status summary\n");

    ck_set_color(CK_COLOR_YELLOW);
    ck_puts("[ FILES ]\n");
    ck_reset_color();
    ck_puts("  ls [path]             list directory contents\n");
    ck_puts("  cat <path>            print file contents\n");
    ck_puts("  stat <path>           show file or directory metadata\n");
    ck_puts("  touch <path>          create an empty file\n");
    ck_puts("  write <path> <text>   write text to a file (overwrites)\n");
    ck_puts("  cp <src> <dst>        copy a file\n");
    ck_puts("  mv <src> <dst>        move/rename a file or directory\n");
    ck_puts("  rm [-r] <path>        remove a file (or directory with -r)\n");
    ck_puts("  tree [path]           show directory tree (default: cwd)\n");
    ck_puts("  du [path]             show recursive disk usage (bytes)\n");
    ck_puts("  mkdir <path>          create a directory\n");
    ck_puts("  rmdir <path>          remove an empty directory\n");
    ck_puts("  hexdump <path>        hex dump file contents\n");
    ck_puts("  cd [path]             change working directory\n");
    ck_puts("  pwd                   print working directory\n");

    ck_set_color(CK_COLOR_YELLOW);
    ck_puts("[ TASKS + SYSTEM ]\n");
    ck_reset_color();
    ck_puts("  history               show command history\n");
    ck_puts("  uptime                show system uptime\n");
    ck_puts("  whoami                print current username\n");
    ck_puts("  hostname              print system hostname\n");
    ck_puts("  ps                    list running tasks\n");
    ck_puts("  tasks                 show task state summary\n");
    ck_puts("  sleep <n>             sleep for n seconds\n");
    ck_puts("  reboot                reboot the system\n");
    ck_puts("  halt                  halt the system\n");
    ck_puts("  setup                 run interactive installer-style setup\n");
    ck_puts("  setup-guide           display hardware installation guide\n");
    ck_puts("  setup-alpine          alias for setup\n");
    ck_puts("  arch-install          legacy alias for setup\n");
    ck_puts("  sudo <command>        execute command as root (live environment)\n");
    ck_puts("  netinfo               show boot-time network status\n");
    ck_puts("  ssh                   show current SSH support status\n");
    ck_puts("  mouse                 show PS/2 mouse status and position\n");
    ck_puts("  scroll [up|down|top|bottom]  navigate shell scrollback buffer (default: up)\n");
    ck_puts("  resolution [WxH|W H] set or show display resolution (default: 80x25)\n");
    ck_puts("  credits               show ComputeKERNEL credits\n");
    ck_puts("  banner                show startup banner again\n");
    ck_puts("  palette               show terminal color palette\n");
    ck_puts("  tips                  show shell productivity tips\n");
    ck_puts("  syscheck              run quick kernel sanity checks\n");
    ck_puts("  kblayout              configure keyboard layout interactively\n");
    ck_puts("  layout                alias for kblayout\n");
    ck_puts("  kblayout list         list available keyboard layouts\n");
    ck_puts("  kblayout set <l> [s]  set layout code and optional sublayout\n");
    ck_puts("  PgUp/PgDn             scroll shell output history\n");
    ck_set_color(CK_COLOR_LIGHT_CYAN);
    ck_puts("+-------------------------------------------------------------+\n");
    ck_reset_color();
}

static void cmd_clear(void)
{
    ck_console_clear();
}

static void cmd_uname(void)
{
    ck_puts("ComputeKERNEL 1.0.0 #1 SMP x86_64 GNU/Linux\n");
}

static void cmd_fastfetch(void)
{
    u64 free_pages  = pmm_free_pages();
    u64 total_pages = pmm_total_pages();
    u64 used_pages  = total_pages - free_pages;
    u64 ticks       = pit_get_ticks();
    u64 uptime_s    = ticks / PIT_HZ;
    u64 uptime_m    = uptime_s / 60;
    u64 uptime_h    = uptime_m / 60;

    ck_puts("\n");

    ck_set_color(CK_COLOR_LIGHT_GREEN);
    ck_puts("                  @@@@@@@@@@          @@@@@@@@@@@@\n");
    ck_puts("                 @@@@@@@@@@@@       @@@@@@@@@@@@@@\n");
    ck_puts("                 @@@@@@@@@@@@     @@@@@@@@@@@@@@@\n");
    ck_puts("                 @@@@@@@@@@@@   @@@@@@@@@@@@@@@\n");
    ck_puts("                 @@@@@@@@@@@@  @@@@@@@@@@@@@@=\n");
    ck_puts("                 @@@@@@@@@@@ @@@@@@@@@@@@@@@\n");
    ck_puts("                 @@@@@@@@@ #@@ @@@@@@@@@@@\n");
    ck_puts("                 @@@@@@@  @@@      @@@@@%\n");
    ck_puts("                 @@@     @@@@       .@@\n");
    ck_puts("                 @@@     @@@@       .@@\n");
    ck_puts("                 @@@@@@@  @@@      @@@@@@\n");
    ck_puts("                 @@@@@@@@@ %@@ @@@@@@@@@@@\n");
    ck_puts("                 @@@@@@@@@@@ @@@@@@@@@@@@@@@\n");
    ck_puts("                 @@@@@@@@@@@@  @@@@@@@@@@@@@@=\n");
    ck_puts("                 @@@@@@@@@@@@   @@@@@@@@@@@@@@@\n");
    ck_puts("                 @@@@@@@@@@@@     @@@@@@@@@@@@@@@\n");
    ck_puts("                 @@@@@@@@@@@@       @@@@@@@@@@@@@@\n");
    ck_puts("                  @@@@@@@@@@          @@@@@@@@@@@@\n");
    ck_reset_color();
    ck_puts("\n");
    ck_puts(SHELL_USER "@" SHELL_HOSTNAME "\n");
    ck_puts("-----------------------------\n");
    ck_puts("OS:       ComputeKERNEL 1.0.0\n");
    ck_puts("Arch:     x86_64\n");
    ck_puts("Kernel:   ComputeKERNEL v1.0.0\n");
    ck_puts("CPU:      x86_64\n");
    ck_puts("Shell:    cksh 1.0.0\n");

    /* Lines 8+ : no art, just info */
    ck_puts("                   Hostname: " SHELL_HOSTNAME "\n");
    u32 display_w = 0, display_h = 0;
    ck_display_get_resolution(&display_w, &display_h);
    ck_printk("                   Terminal: VGA %ux%u\n",
              (unsigned int)display_w, (unsigned int)display_h);
    ck_printk("                   Uptime:   %llu:%02llu:%02llu\n",
              uptime_h, uptime_m % 60, uptime_s % 60);
    ck_printk("                   Memory:   %llu / %llu KiB used\n",
              used_pages * 4ULL, total_pages * 4ULL);
    ck_printk("                   Free:     %llu KiB (%llu pages)\n",
              free_pages * 4ULL, free_pages);
    ck_puts("\n");
}

static void cmd_free(void)
{
    u64 free_pages = pmm_free_pages();
    ck_printk("free: %llu pages  (%llu KiB / %llu MiB)\n",
              free_pages, free_pages * 4ULL, (free_pages * 4ULL) >> 10);
}

static void cmd_mem(void)
{
    u64 free_p  = pmm_free_pages();
    u64 total_p = pmm_total_pages();
    u64 used_p  = total_p - free_p;
    ck_puts("          total        used        free\n");
    ck_printk("Mem:  %8llu KiB %8llu KiB %8llu KiB\n",
              total_p * 4ULL, used_p * 4ULL, free_p * 4ULL);
    ck_printk("      %8llu MiB %8llu MiB %8llu MiB\n",
              (total_p * 4ULL) >> 10, (used_p * 4ULL) >> 10,
              (free_p * 4ULL) >> 10);
}

static void cmd_ls(const char *path)
{
    char resolved[VFS_NAME_MAX];
    if (path_resolve_or_error("ls", path, resolved) < 0)
        return;

    struct vfs_node *node = vfs_lookup(resolved);
    if (!node) {
        ck_printk("ls: %s: No such file or directory\n",
                  (path && *path) ? path : resolved);
        return;
    }

    /* If it's a plain file, just list it */
    if (node->type == VFS_FILE) {
        ck_printk("  %-32s  %llu bytes\n", node->name, node->size);
        return;
    }

    int fd = vfs_open(resolved, O_RDONLY);
    if (fd < 0) {
        ck_printk("ls: %s: cannot open\n", resolved);
        return;
    }

    char name[VFS_NAME_MAX];
    int found = 0;
    for (u32 i = 0; vfs_readdir(fd, i, name) == 0; i++) {
        /* Build entry path to get metadata */
        char epath[VFS_NAME_MAX];
        size_t rlen = strlen(resolved);
        size_t nlen = strlen(name);
        if (rlen + 1 + nlen < VFS_NAME_MAX) {
            memcpy(epath, resolved, rlen);
            if (epath[rlen - 1] != '/')
                epath[rlen++] = '/';
            memcpy(epath + rlen, name, nlen + 1);
            struct vfs_node *child = vfs_lookup(epath);
            if (child && child->type == VFS_DIR)
                ck_printk("  %s/\n", name);
            else if (child)
                ck_printk("  %-32s  %llu bytes\n", name, child->size);
            else
                ck_printk("  %s\n", name);
        } else {
            ck_printk("  %s\n", name);
        }
        found = 1;
    }
    if (!found)
        ck_puts("  (empty)\n");
    vfs_close(fd);
}

static void cmd_cat(const char *path)
{
    if (!path || !*path) {
        ck_puts("cat: missing file operand\n");
        return;
    }

    char resolved[VFS_NAME_MAX];
    if (path_resolve_or_error("cat", path, resolved) < 0)
        return;

    int fd = vfs_open(resolved, O_RDONLY);
    if (fd < 0) {
        ck_printk("cat: %s: No such file or directory\n", resolved);
        return;
    }

    char buf[128];
    ssize_t n;
    while ((n = vfs_read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        ck_puts(buf);
    }
    vfs_close(fd);
}

static void cmd_stat(const char *path)
{
    if (!path || !*path) {
        ck_puts("stat: missing file operand\n");
        return;
    }

    char resolved[VFS_NAME_MAX];
    if (path_resolve_or_error("stat", path, resolved) < 0)
        return;

    struct vfs_node *node = vfs_lookup(resolved);
    if (!node) {
        ck_printk("stat: %s: No such file or directory\n", resolved);
        return;
    }

    ck_printk("  File: %s\n", node->name);
    ck_printk("  Path: %s\n", resolved);
    ck_printk("  Type: %s\n",
              node->type == VFS_DIR ? "directory" : "regular file");
    ck_printk("  Size: %llu bytes\n", node->size);
}

static void cmd_touch(const char *path)
{
    if (!path || !*path) {
        ck_puts("touch: missing file operand\n");
        return;
    }

    char resolved[VFS_NAME_MAX];
    if (path_resolve_or_error("touch", path, resolved) < 0)
        return;

    int fd = vfs_open(resolved, O_CREAT | O_WRONLY);
    if (fd < 0) {
        ck_printk("touch: %s: cannot create file\n", resolved);
        return;
    }
    vfs_close(fd);
}

static void cmd_write(const char *args)
{
    if (!args || !*args) {
        ck_puts("write: usage: write <path> <text>\n");
        return;
    }

    /* Split at first space: path<SPACE>text */
    const char *p = args;
    while (*p && *p != ' ') p++;
    if (!*p) {
        ck_puts("write: usage: write <path> <text>\n");
        return;
    }

    char path[VFS_NAME_MAX];
    size_t pathlen = (size_t)(p - args);
    if (pathlen >= VFS_NAME_MAX) pathlen = VFS_NAME_MAX - 1;
    memcpy(path, args, pathlen);
    path[pathlen] = '\0';

    const char *text = p + 1;

    char resolved[VFS_NAME_MAX];
    if (path_resolve_or_error("write", path, resolved) < 0)
        return;

    int fd = vfs_open(resolved, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        ck_printk("write: %s: cannot open file\n", resolved);
        return;
    }
    size_t len = strlen(text);
    if (len > 0) vfs_write(fd, text, len);
    vfs_write(fd, "\n", 1);
    vfs_close(fd);
}

static void cmd_cp(const char *args)
{
    if (!args || !*args) {
        ck_puts("cp: usage: cp <src> <dst>\n");
        return;
    }

    const char *p = args;
    while (*p && *p != ' ') p++;
    if (!*p) {
        ck_puts("cp: usage: cp <src> <dst>\n");
        return;
    }

    char src[VFS_NAME_MAX];
    size_t srclen = (size_t)(p - args);
    if (srclen >= VFS_NAME_MAX) srclen = VFS_NAME_MAX - 1;
    memcpy(src, args, srclen);
    src[srclen] = '\0';

    const char *dst_arg = p + 1;
    while (*dst_arg == ' ') dst_arg++;

    char src_res[VFS_NAME_MAX], dst_res[VFS_NAME_MAX];
    if (path_resolve_or_error("cp", src, src_res) < 0 ||
        path_resolve_or_error("cp", dst_arg, dst_res) < 0)
        return;

    int in = vfs_open(src_res, O_RDONLY);
    if (in < 0) {
        ck_printk("cp: %s: No such file or directory\n", src_res);
        return;
    }

    int out = vfs_open(dst_res, O_WRONLY | O_CREAT | O_TRUNC);
    if (out < 0) {
        vfs_close(in);
        ck_printk("cp: %s: cannot create\n", dst_res);
        return;
    }

    char buf[128];
    ssize_t n;
    while ((n = vfs_read(in, buf, sizeof(buf))) > 0)
        vfs_write(out, buf, (size_t)n);

    vfs_close(in);
    vfs_close(out);
}

static void cmd_mv(const char *args)
{
    if (!args || !*args) {
        ck_puts("mv: usage: mv <src> <dst>\n");
        return;
    }

    const char *p = args;
    while (*p && *p != ' ') p++;
    if (!*p) {
        ck_puts("mv: usage: mv <src> <dst>\n");
        return;
    }

    char src[VFS_NAME_MAX];
    size_t srclen = (size_t)(p - args);
    if (srclen >= VFS_NAME_MAX) srclen = VFS_NAME_MAX - 1;
    memcpy(src, args, srclen);
    src[srclen] = '\0';

    const char *dst_arg = p + 1;
    while (*dst_arg == ' ') dst_arg++;

    char src_res[VFS_NAME_MAX], dst_res[VFS_NAME_MAX];
    if (path_resolve_or_error("mv", src, src_res) < 0 ||
        path_resolve_or_error("mv", dst_arg, dst_res) < 0)
        return;

    if (vfs_rename(src_res, dst_res) < 0)
        ck_printk("mv: cannot rename '%s' to '%s'\n", src_res, dst_res);
}

static int rm_recursive(const char *path)
{
    struct vfs_node *node = vfs_lookup(path);
    if (!node)
        return -1;
    if (node->type != VFS_DIR)
        return vfs_unlink(path);

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    char name[VFS_NAME_MAX];
    while (vfs_readdir(fd, 0, name) == 0) {
        char child[VFS_NAME_MAX];
        size_t plen = strlen(path);
        size_t nlen = strlen(name);
        if (plen + 1 + nlen >= VFS_NAME_MAX) {
            vfs_close(fd);
            return -1;
        }
        memcpy(child, path, plen);
        if (child[plen - 1] != '/')
            child[plen++] = '/';
        memcpy(child + plen, name, nlen + 1);
        if (rm_recursive(child) < 0) {
            vfs_close(fd);
            return -1;
        }
    }
    vfs_close(fd);
    return vfs_unlink(path);
}

static void cmd_rm(const char *args)
{
    if (!args || !*args) {
        ck_puts("rm: missing operand\n");
        return;
    }

    int is_recursive = 0;
    int force = 0;
    const char *path = args;
    while (*path == ' ')
        path++;
    while (*path == '-') {
        if (path[1] == '-' && (path[2] == '\0' || path[2] == ' ')) {
            path += 2;
            while (*path == ' ')
                path++;
            break;
        }
        if (path[1] == '\0' || path[1] == ' ')
            break;
        path++;
        while (*path && *path != ' ') {
            if (*path == 'r')
                is_recursive = 1;
            else if (*path == 'f')
                force = 1;
            else {
                ck_printk("rm: invalid option -- '%c'\n", *path);
                return;
            }
            path++;
        }
        while (*path == ' ')
            path++;
    }

    if (!*path) {
        ck_puts("rm: missing operand\n");
        return;
    }

    char resolved[VFS_NAME_MAX];
    if (path_resolve_or_error("rm", path, resolved) < 0)
        return;

    struct vfs_node *node = vfs_lookup(resolved);
    if (!node) {
        if (!force)
            ck_printk("rm: %s: No such file or directory\n", resolved);
        return;
    }
    if (node->type == VFS_DIR && !is_recursive) {
        ck_printk("rm: %s: is a directory (use rm -r)\n", resolved);
        return;
    }
    if ((is_recursive ? rm_recursive(resolved) : vfs_unlink(resolved)) < 0 && !force)
        ck_printk("rm: %s: cannot remove\n", resolved);
}

static void cmd_mkdir(const char *path)
{
    if (!path || !*path) {
        ck_puts("mkdir: missing operand\n");
        return;
    }

    char resolved[VFS_NAME_MAX];
    if (path_resolve_or_error("mkdir", path, resolved) < 0)
        return;

    if (vfs_mkdir(resolved) < 0)
        ck_printk("mkdir: %s: cannot create directory\n", resolved);
}

static void cmd_rmdir(const char *path)
{
    if (!path || !*path) {
        ck_puts("rmdir: missing operand\n");
        return;
    }

    char resolved[VFS_NAME_MAX];
    if (path_resolve_or_error("rmdir", path, resolved) < 0)
        return;

    struct vfs_node *node = vfs_lookup(resolved);
    if (!node) {
        ck_printk("rmdir: %s: No such file or directory\n", resolved);
        return;
    }
    if (node->type != VFS_DIR) {
        ck_printk("rmdir: %s: Not a directory\n", resolved);
        return;
    }
    if (vfs_unlink(resolved) < 0)
        ck_printk("rmdir: %s: cannot remove (may not be empty)\n", resolved);
}

static void cmd_hexdump(const char *path)
{
    if (!path || !*path) {
        ck_puts("hexdump: missing file operand\n");
        return;
    }

    char resolved[VFS_NAME_MAX];
    if (path_resolve_or_error("hexdump", path, resolved) < 0)
        return;

    int fd = vfs_open(resolved, O_RDONLY);
    if (fd < 0) {
        ck_printk("hexdump: %s: No such file or directory\n", resolved);
        return;
    }

    u8    buf[16];
    u64   offset = 0;
    ssize_t n;
    while ((n = vfs_read(fd, buf, sizeof(buf))) > 0) {
        ck_printk("%08llx  ", offset);
        for (ssize_t i = 0; i < 16; i++) {
            if (i < n)
                ck_printk("%02x ", (unsigned int)(unsigned char)buf[i]);
            else
                ck_puts("   ");
            if (i == 7) ck_puts(" ");
        }
        ck_puts(" |");
        for (ssize_t i = 0; i < n; i++) {
            char c = (char)buf[i];
            ck_putchar((c >= 0x20 && c < 0x7f) ? c : '.');
        }
        ck_puts("|\n");
        offset += (u64)n;
    }
    if (offset == 0)
        ck_puts("(empty file)\n");
    vfs_close(fd);
}

static void tree_print(const char *path, int depth)
{
    if (depth > SHELL_FS_RECURSE_MAX)
        return;

    struct vfs_node *node = vfs_lookup(path);
    if (!node)
        return;

    for (int i = 0; i < depth; i++)
        ck_puts("  ");

    int is_root = (depth == 0 && path[0] == '/' && path[1] == '\0');
    if (is_root)
        ck_puts("/\n");
    else
        ck_printk("%s%s\n", node->name, (node->type == VFS_DIR) ? "/" : "");

    if (node->type != VFS_DIR)
        return;

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0)
        return;

    char child_name[VFS_NAME_MAX];
    char child_path[VFS_NAME_MAX];
    for (u32 i = 0; vfs_readdir(fd, i, child_name) == 0; i++) {
        size_t plen = strlen(path);
        size_t nlen = strlen(child_name);
        if (plen + 1 + nlen >= VFS_NAME_MAX)
            continue;
        memcpy(child_path, path, plen);
        if (child_path[plen - 1] != '/')
            child_path[plen++] = '/';
        memcpy(child_path + plen, child_name, nlen + 1);
        tree_print(child_path, depth + 1);
    }

    vfs_close(fd);
}

static void cmd_tree(const char *path)
{
    char resolved[VFS_NAME_MAX];
    if (path_resolve_or_error("tree", path, resolved) < 0)
        return;

    struct vfs_node *node = vfs_lookup(resolved);
    if (!node) {
        ck_printk("tree: %s: No such file or directory\n",
                  (path && *path) ? path : resolved);
        return;
    }

    tree_print(resolved, 0);
}

static u64 du_size(const char *path, int depth)
{
    if (depth > SHELL_FS_RECURSE_MAX)
        return 0;

    struct vfs_node *node = vfs_lookup(path);
    if (!node)
        return 0;
    if (node->type != VFS_DIR)
        return node->size;

    u64 total = 0;
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0)
        return 0;

    char child_name[VFS_NAME_MAX];
    char child_path[VFS_NAME_MAX];
    for (u32 i = 0; vfs_readdir(fd, i, child_name) == 0; i++) {
        size_t plen = strlen(path);
        size_t nlen = strlen(child_name);
        if (plen + 1 + nlen >= VFS_NAME_MAX)
            continue;
        memcpy(child_path, path, plen);
        if (child_path[plen - 1] != '/')
            child_path[plen++] = '/';
        memcpy(child_path + plen, child_name, nlen + 1);
        total += du_size(child_path, depth + 1);
    }

    vfs_close(fd);
    return total;
}

static void cmd_du(const char *path)
{
    char resolved[VFS_NAME_MAX];
    if (path_resolve_or_error("du", path, resolved) < 0)
        return;

    struct vfs_node *node = vfs_lookup(resolved);
    if (!node) {
        ck_printk("du: %s: No such file or directory\n",
                  (path && *path) ? path : resolved);
        return;
    }

    u64 size = du_size(resolved, 0);
    ck_printk("%llu\t%s\n", size, resolved);
}

static void cmd_cd(const char *path)
{
    char resolved[VFS_NAME_MAX];

    if (!path || !*path) {
        /* cd with no args: go to root */
        shell_cwd[0] = '/'; shell_cwd[1] = '\0';
        return;
    }
    if (path_resolve_or_error("cd", path, resolved) < 0)
        return;

    struct vfs_node *node = vfs_lookup(resolved);
    if (!node) {
        ck_printk("cd: %s: No such file or directory\n", resolved);
        return;
    }
    if (node->type != VFS_DIR) {
        ck_printk("cd: %s: Not a directory\n", resolved);
        return;
    }
    strncpy(shell_cwd, resolved, VFS_NAME_MAX - 1);
    shell_cwd[VFS_NAME_MAX - 1] = '\0';
}

static void cmd_uptime(void)
{
    u64 ticks  = pit_get_ticks();
    u64 secs   = ticks / PIT_HZ;
    u64 mins   = secs / 60;
    u64 hrs    = mins / 60;
    ck_printk("up %llu:%02llu:%02llu  (%llu ticks)\n",
              hrs, mins % 60, secs % 60, ticks);
}

static void cmd_whoami(void)
{
    ck_puts(SHELL_USER "\n");
}

static void cmd_hostname(void)
{
    ck_puts(SHELL_HOSTNAME "\n");
}

static const char *task_state_str(int state)
{
    switch (state) {
    case TASK_RUNNING: return "running";
    case TASK_READY:   return "ready";
    case TASK_BLOCKED: return "blocked";
    case TASK_DEAD:    return "dead";
    default:           return "unknown";
    }
}

static void cmd_ps(void)
{
    int n = sched_num_tasks();
    ck_puts("  TID  STATE    TICKS      NAME\n");
    ck_puts("  ---  ------   ---------  ----\n");
    for (int i = 0; i < n; i++) {
        struct task *t = sched_get_task(i);
        if (!t)
            continue;
        ck_printk("  %3d  %-8s %9llu  %s\n",
                  t->tid, task_state_str(t->state), t->ticks, t->name);
    }
}

static void cmd_sleep(const char *arg)
{
    if (!arg || !*arg) {
        ck_puts("sleep: missing operand\n");
        return;
    }
    u64 secs = 0;
    const char *p = arg;
    /* Reject anything that isn't purely decimal digits */
    if (*p < '0' || *p > '9') {
        ck_puts("sleep: invalid interval\n");
        return;
    }
    while (*p >= '0' && *p <= '9') {
        secs = secs * 10 + (u64)(*p++ - '0');
        /* Guard against overflow: 2^64/10 ≈ 1.8e18, cap at a sane max */
        if (secs > 86400ULL) {
            ck_puts("sleep: interval too large (max 86400 s)\n");
            return;
        }
    }
    if (*p != '\0') {
        ck_puts("sleep: invalid interval\n");
        return;
    }
    pit_sleep_ms(secs * 1000ULL);
}

static void cmd_pwd(void)
{
    ck_puts(shell_cwd);
    ck_putchar('\n');
}

static void cmd_history(void)
{
    if (shell_hist_count == 0) {
        ck_puts("  (no history)\n");
        return;
    }
    int start = (shell_hist_count == SHELL_HIST_MAX) ? shell_hist_head : 0;
    for (int i = 0; i < shell_hist_count; i++) {
        int idx = (start + i) % SHELL_HIST_MAX;
        ck_printk("  %3d  %s\n", i + 1, shell_hist[idx]);
    }
}

static void cmd_reboot(void)
{
    ck_puts("Rebooting...\n");
    /* Pulse the keyboard controller reset line */
    while (inb(KBD_STATUS_PORT) & KBD_STATUS_IBF)
        ;
    outb(KBD_CMD_PORT, KBD_CMD_RESET);
    /* If that fails, spin */
    for (;;)
        __asm__ __volatile__("cli; hlt");
}

static void cmd_halt(void)
{
    ck_puts("System halted.\n");
    __asm__ __volatile__("cli; hlt");
    for (;;) {}
}

static void cmd_setup(void)
{
    cmd_installer_style();
}

static void cmd_setup_guide(void)
{
    ck_set_color(CK_COLOR_LIGHT_CYAN);
    ck_puts("ComputeKERNEL Live Setup Guide\n");
    ck_reset_color();
    ck_puts("===================================\n\n");

    ck_set_color(CK_COLOR_YELLOW);
    ck_puts("Requirements:\n");
    ck_reset_color();
    ck_puts("  - x86_64 compatible PC (64-bit CPU)\n");
    ck_puts("  - At least 32 MiB of RAM\n");
    ck_puts("  - USB drive (1 GiB or larger)\n");
    ck_puts("  - BIOS or UEFI with legacy/CSM boot support\n");
    ck_puts("  - Live environment boots as root by default\n\n");

    ck_set_color(CK_COLOR_YELLOW);
    ck_puts("Steps to boot on real hardware:\n");
    ck_reset_color();
    ck_puts("  1. Download the ComputeKERNEL ISO from the project releases page.\n");
    ck_puts("  2. Flash the ISO to a USB drive:\n");
    ck_set_color(CK_COLOR_LIGHT_GREEN);
    ck_puts("       Linux/macOS: ");
    ck_reset_color();
    ck_puts("sudo dd if=computekernel.iso of=/dev/sdX bs=4M status=progress\n");
    ck_set_color(CK_COLOR_LIGHT_GREEN);
    ck_puts("       Windows:     ");
    ck_reset_color();
    ck_puts("use Rufus (https://rufus.ie) - select ISO, write in DD mode\n");
    ck_puts("  3. Insert the USB drive into the target machine.\n");
    ck_puts("  4. Enter BIOS/UEFI firmware setup (usually F2, F12, DEL, or ESC).\n");
    ck_puts("  5. Set the boot order so the USB drive boots first.\n");
    ck_puts("  6. Disable Secure Boot if it is enabled.\n");
    ck_puts("  7. Save settings and reboot - ComputeKERNEL will load from USB.\n\n");

    ck_set_color(CK_COLOR_YELLOW);
    ck_puts("Notes:\n");
    ck_reset_color();
    ck_puts("  - Live session runs as root.\n");
    ck_puts("  - Default root passwords for live testing: 'toor' or 'password'.\n");
    ck_puts("  - ComputeKERNEL currently runs entirely from RAM.\n");
    ck_puts("  - All files and changes are lost when the machine is powered off.\n");
    ck_puts("  - Use 'reboot' or 'halt' to cleanly exit the kernel.\n\n");
    ck_puts("Use 'setup' for the interactive setup flow.\n");
}

static int shell_readline(char *buf, int max_len)
{
    int pos = 0;
    if (!buf || max_len <= 1)
        return 0;
    buf[0] = '\0';
    for (;;) {
        int c = keyboard_getchar();
        if (!c) {
            sched_yield();
            continue;
        }
        if (c == KEY_CTRL_C) {
            ck_puts("^C\n");
            buf[0] = '\0';
            return -1;
        }
        if (c == '\b') {
            if (pos > 0) {
                pos--;
                ck_putchar('\b');
            }
            continue;
        }
        if (c == '\n' || c == '\r') {
            ck_putchar('\n');
            buf[pos] = '\0';
            return pos;
        }
        if ((unsigned char)c >= ASCII_SPACE && (unsigned char)c < ASCII_DEL) {
            if (pos < max_len - 1) {
                buf[pos++] = (char)c;
                ck_putchar((char)c);
            }
        }
    }
}

static void cmd_kblayout_list(void)
{
    int n = keyboard_get_layout_count();
    ck_printk("current layout: %s", keyboard_current_layout());
    if (keyboard_current_sublayout() && *keyboard_current_sublayout())
        ck_printk(" (%s)", keyboard_current_sublayout());
    ck_putchar('\n');
    for (int i = 0; i < n; i++) {
        const char *code = keyboard_get_layout_code(i);
        const char *desc = keyboard_get_layout_description(i);
        ck_printk("  %-8s %s\n", code ? code : "?", desc ? desc : "");
        int subn = keyboard_get_sublayout_count(code);
        for (int j = 0; j < subn; j++) {
            const char *sub = keyboard_get_sublayout_name(code, j);
            if (sub)
                ck_printk("           - %s\n", sub);
        }
    }
}

static void cmd_kblayout_set(const char *layout, const char *sublayout)
{
    if (!layout || !*layout) {
        ck_puts("kblayout: usage: kblayout set <layout> [sublayout]\n");
        return;
    }
    int rc = keyboard_set_layout(layout, sublayout);
    if (rc == -1) {
        ck_printk("kblayout: unknown layout '%s'\n", layout);
        return;
    }
    if (rc == -2) {
        ck_printk("kblayout: unknown sublayout '%s' for layout '%s'\n",
                  sublayout ? sublayout : "", layout);
        return;
    }
    ck_printk("kblayout: active layout is now %s", keyboard_current_layout());
    if (keyboard_current_sublayout() && *keyboard_current_sublayout())
        ck_printk(" (%s)", keyboard_current_sublayout());
    ck_putchar('\n');
}

static void cmd_kblayout(const char *args)
{
    if (!args || !*args) {
        char line[SHELL_LINE_MAX];
        ck_puts("Available keyboard layouts:\n");
        cmd_kblayout_list();
        ck_puts("Type layout code (or empty to cancel): ");
        int n = shell_readline(line, SHELL_LINE_MAX);
        if (n <= 0) {
            ck_puts("kblayout: cancelled\n");
            return;
        }
        int subn = keyboard_get_sublayout_count(line);
        if (subn > 1) {
            ck_puts("Available sublayouts:\n");
            for (int i = 0; i < subn; i++) {
                const char *sub = keyboard_get_sublayout_name(line, i);
                if (sub)
                    ck_printk("  - %s\n", sub);
            }
            ck_puts("Choose sublayout (or type 'no' for default): ");
            char subline[SHELL_LINE_MAX];
            int sn = shell_readline(subline, SHELL_LINE_MAX);
            if (sn > 0 && strcmp(subline, "no") != 0)
                cmd_kblayout_set(line, subline);
            else
                cmd_kblayout_set(line, NULL);
            return;
        }
        cmd_kblayout_set(line, NULL);
        return;
    }

    if (strcmp(args, "list") == 0) {
        cmd_kblayout_list();
        return;
    }
    if (strncmp(args, "set ", 4) == 0) {
        const char *rest = args + 4;
        while (*rest == ' ')
            rest++;
        if (!*rest) {
            ck_puts("kblayout: usage: kblayout set <layout> [sublayout]\n");
            return;
        }
        const char *p = rest;
        while (*p && *p != ' ')
            p++;
        char layout[32];
        size_t ll = (size_t)(p - rest);
        if (ll >= sizeof(layout))
            ll = sizeof(layout) - 1;
        memcpy(layout, rest, ll);
        layout[ll] = '\0';
        while (*p == ' ')
            p++;
        cmd_kblayout_set(layout, *p ? p : NULL);
        return;
    }
    ck_puts("kblayout: usage: kblayout [list|set <layout> [sublayout]]\n");
}

static void cmd_sudo(const char *args)
{
    if (!args || !*args) {
        ck_puts("sudo: usage: sudo <command>\n");
        return;
    }
    char cmd[SHELL_LINE_MAX];
    strncpy(cmd, args, SHELL_LINE_MAX - 1);
    cmd[SHELL_LINE_MAX - 1] = '\0';
    shell_exec(cmd);
}

static void cmd_installer_style(void)
{
    char line[SHELL_LINE_MAX];
    ck_puts("ComputeKERNEL setup-alpine-style installer (live preview)\n");
    ck_puts("Target mode: live root environment\n");

    ck_puts("Select keyboard layout (run 'kblayout list' for full list): ");
    int n = shell_readline(line, SHELL_LINE_MAX);
    if (n > 0) {
        int subn = keyboard_get_sublayout_count(line);
        if (subn > 1) {
            ck_puts("Use sublayout? type name or 'no': ");
            char subline[SHELL_LINE_MAX];
            int sn = shell_readline(subline, SHELL_LINE_MAX);
            if (sn > 0 && strcmp(subline, "no") != 0)
                cmd_kblayout_set(line, subline);
            else
                cmd_kblayout_set(line, NULL);
        } else {
            cmd_kblayout_set(line, NULL);
        }
    }

    ck_puts("Set root password [toor/password/custom] (Enter for toor): ");
    n = shell_readline(line, SHELL_LINE_MAX);
    if (n <= 0 || strcmp(line, "toor") == 0 || strcmp(line, "password") == 0) {
        ck_puts("root password set for live session profile.\n");
    } else {
        ck_puts("custom root password noted for this setup session.\n");
    }
    ck_puts("Setup complete. You are running as root in live environment.\n");
}

static void cmd_echo(const char *rest)
{
    if (rest && *rest)
        ck_printk("%s\n", rest);
    else
        ck_putchar('\n');
}

static void cmd_credits(void)
{
    ck_puts("ComputeKERNEL credits:\n");
    ck_puts("  - Marcel R. (m4rcel-lol on GitHub) — creator and project owner\n");
    ck_puts("  - OpenAI Coding Agent — implementation assistance\n");
}

static void cmd_mouse(void)
{
    int x = 0, y = 0;
    u8 buttons = 0;
    if (!mouse_is_available()) {
        ck_puts("mouse: PS/2 mouse not available\n");
        return;
    }
    mouse_get_position(&x, &y, &buttons);
    ck_printk("mouse: enabled  pos=(%d,%d)  buttons=0x%x\n", x, y, (unsigned int)buttons);
}

static void cmd_netinfo(void)
{
    const u8 *packet = ck_boot_network_packet_data();
    u32 packet_size = ck_boot_network_packet_size();
    ck_puts("netinfo: guest networking support\n");
    ck_puts("  outbound: available with QEMU user networking (NAT)\n");
    ck_puts("  inbound : hostfwd tcp localhost:2222 -> guest:22\n");
    if (!ck_network_available()) {
        ck_puts("  boot pkt: not provided by bootloader\n");
        return;
    }
    ck_printk("  boot pkt: available (%u bytes)\n", packet_size);

    if (packet && packet_size >= 14) {
        u16 ethertype = ((u16)packet[12] << 8) | packet[13];
        ck_printk("  frame   : dst %02x:%02x:%02x:%02x:%02x:%02x\n",
                  packet[0], packet[1], packet[2], packet[3], packet[4], packet[5]);
        ck_printk("            src %02x:%02x:%02x:%02x:%02x:%02x\n",
                  packet[6], packet[7], packet[8], packet[9], packet[10], packet[11]);
        ck_printk("            ethertype 0x%04x\n", (unsigned int)ethertype);
    }
    ck_puts("  status  : boot-time packet visibility is active in-kernel\n");
}

static void cmd_ssh(void)
{
    ck_puts("ssh: secure shell access overview\n");
    ck_puts("  host-side : TCP localhost:2222 -> guest:22 (QEMU user networking)\n");
    ck_puts("  in-kernel : TCP/IP stack not available in current kernel build\n");
    ck_puts("  connect   : use host command ssh -p 2222 root@localhost\n");
}

static void cmd_motd(void)
{
    u64 ticks = pit_get_ticks();
    u64 uptime_s = ticks / PIT_HZ;
    u64 uptime_m = uptime_s / 60;
    u64 uptime_h = uptime_m / 60;
    u64 free_pages  = pmm_free_pages();
    u64 total_pages = pmm_total_pages();
    u64 used_pages  = total_pages - free_pages;
    u32 display_w = 0, display_h = 0;
    ck_display_get_resolution(&display_w, &display_h);

    ck_set_color(CK_COLOR_LIGHT_CYAN);
    ck_puts("+-------------------- session status --------------------+\n");
    ck_reset_color();
    ck_printk(" user      : %s\n", SHELL_USER);
    ck_printk(" host      : %s\n", SHELL_HOSTNAME);
    ck_printk(" cwd       : %s\n", shell_cwd);
    ck_printk(" terminal  : VGA %ux%u\n", (unsigned int)display_w, (unsigned int)display_h);
    ck_printk(" uptime    : %llu:%02llu:%02llu\n", uptime_h, uptime_m % 60, uptime_s % 60);
    ck_printk(" memory    : %llu / %llu KiB used (%llu KiB free)\n",
              used_pages * 4ULL, total_pages * 4ULL, free_pages * 4ULL);
    if (ck_network_available())
        ck_printk(" network   : boot packet available (%u bytes)\n",
                  (unsigned int)ck_boot_network_packet_size());
    else
        ck_puts(" network   : boot packet not available\n");
    ck_set_color(CK_COLOR_LIGHT_CYAN);
    ck_puts("+--------------------------------------------------------+\n");
    ck_reset_color();
}

static void cmd_palette(void)
{
    struct {
        u8 color;
        const char *name;
    } swatches[] = {
        { CK_COLOR_BLACK, "black" },
        { CK_COLOR_BLUE, "blue" },
        { CK_COLOR_GREEN, "green" },
        { CK_COLOR_CYAN, "cyan" },
        { CK_COLOR_RED, "red" },
        { CK_COLOR_MAGENTA, "magenta" },
        { CK_COLOR_BROWN, "brown" },
        { CK_COLOR_LIGHT_GRAY, "light-gray" },
        { CK_COLOR_DARK_GRAY, "dark-gray" },
        { CK_COLOR_LIGHT_BLUE, "light-blue" },
        { CK_COLOR_LIGHT_GREEN, "light-green" },
        { CK_COLOR_LIGHT_CYAN, "light-cyan" },
        { CK_COLOR_LIGHT_RED, "light-red" },
        { CK_COLOR_PINK, "pink" },
        { CK_COLOR_YELLOW, "yellow" },
        { CK_COLOR_WHITE, "white" },
    };
    const u32 swatches_count = (u32)(sizeof(swatches) / sizeof(swatches[0]));
    ck_puts("palette: VGA text colors\n");
    for (u32 i = 0; i < swatches_count; i++) {
        ck_set_color(swatches[i].color);
        ck_printk("  [%2u] %s", (unsigned int)swatches[i].color, swatches[i].name);
        ck_reset_color();
        ck_puts("\n");
    }
}

static void cmd_tips(void)
{
    ck_puts("cksh quick tips:\n");
    ck_puts("  - Use Up/Down arrows for command history\n");
    ck_puts("  - Use Ctrl+X to cut the current line, Ctrl+V to paste\n");
    ck_puts("  - Use PgUp/PgDn or 'scroll' to navigate shell output\n");
    ck_puts("  - Use 'motd' for a compact status summary\n");
    ck_puts("  - Use 'fastfetch' for full system overview\n");
    ck_puts("  - Use 'kblayout' to switch keyboard layout interactively\n");
}

static void cmd_tasks(void)
{
    int n = sched_num_tasks();
    u32 running = 0, ready = 0, blocked = 0, dead = 0, unknown = 0;
    u64 total_ticks = 0;
    const struct task *current = sched_current();

    for (int i = 0; i < n; i++) {
        struct task *t = sched_get_task(i);
        if (!t)
            continue;
        total_ticks += t->ticks;
        switch (t->state) {
        case TASK_RUNNING: running++; break;
        case TASK_READY:   ready++; break;
        case TASK_BLOCKED: blocked++; break;
        case TASK_DEAD:    dead++; break;
        default:           unknown++; break;
        }
    }

    ck_set_color(CK_COLOR_LIGHT_CYAN);
    ck_puts("+--------------------- task summary ---------------------+\n");
    ck_reset_color();
    ck_printk(" total tasks : %d\n", n);
    ck_printk(" running     : %u\n", (unsigned int)running);
    ck_printk(" ready       : %u\n", (unsigned int)ready);
    ck_printk(" blocked     : %u\n", (unsigned int)blocked);
    ck_printk(" dead        : %u\n", (unsigned int)dead);
    if (unknown)
        ck_printk(" unknown     : %u\n", (unsigned int)unknown);
    ck_printk(" cpu ticks   : %llu (sum of all task ticks)\n", total_ticks);
    if (current)
        ck_printk(" current     : tid=%d name=%s\n", current->tid, current->name);
    ck_set_color(CK_COLOR_LIGHT_CYAN);
    ck_puts("+--------------------------------------------------------+\n");
    ck_reset_color();
}

static void cmd_syscheck(void)
{
    u32 display_w = 0, display_h = 0;
    ck_display_get_resolution(&display_w, &display_h);
    u64 free_pages  = pmm_free_pages();
    u64 total_pages = pmm_total_pages();
    u64 ticks = pit_get_ticks();

    int ok = 1;
    ck_set_color(CK_COLOR_LIGHT_CYAN);
    ck_puts("syscheck: kernel sanity checks\n");
    ck_reset_color();

    if (display_w == 0 || display_h == 0) {
        ck_puts("  [FAIL] display resolution unavailable\n");
        ok = 0;
    } else {
        ck_printk("  [ OK ] display resolution %ux%u\n",
                  (unsigned int)display_w, (unsigned int)display_h);
    }

    if (total_pages == 0 || free_pages > total_pages) {
        ck_puts("  [FAIL] memory accounting invalid\n");
        ok = 0;
    } else {
        ck_printk("  [ OK ] memory pages free=%llu total=%llu\n", free_pages, total_pages);
    }

    if (sched_num_tasks() <= 0) {
        ck_puts("  [FAIL] scheduler task list empty\n");
        ok = 0;
    } else {
        ck_printk("  [ OK ] scheduler tasks visible (%d)\n", sched_num_tasks());
    }

    if (ticks == 0) {
        ck_puts("  [WARN] PIT tick count is 0 (very early boot?)\n");
    } else {
        ck_printk("  [ OK ] timer ticks=%llu\n", ticks);
    }

    if (ok) {
        ck_set_color(CK_COLOR_LIGHT_GREEN);
        ck_puts("syscheck: all critical checks passed\n");
    } else {
        ck_set_color(CK_COLOR_LIGHT_RED);
        ck_puts("syscheck: one or more critical checks failed\n");
    }
    ck_reset_color();
}

static void shell_print_banner(void)
{
    ck_puts("\n");
    ck_set_color(CK_COLOR_LIGHT_GREEN);
    ck_puts("   ######   ##   ##\n");
    ck_puts("  ##       ## ## ##   ComputeKERNEL\n");
    ck_puts("  ##       ##  ###    Lightweight x86_64 OS shell\n");
    ck_puts("  ##       ## ## ##\n");
    ck_puts("   ######  ##   ##\n");
    ck_reset_color();
}

static void cmd_scroll(const char *args)
{
    if (!args || !*args || strcmp(args, "up") == 0) {
        ck_console_scroll_up(10);
        return;
    }
    if (strcmp(args, "down") == 0) {
        ck_console_scroll_down(10);
        return;
    }
    if (strcmp(args, "top") == 0) {
        ck_console_scroll_up(SHELL_SCROLL_TO_TOP_LINES);
        return;
    }
    if (strcmp(args, "reset") == 0 || strcmp(args, "bottom") == 0) {
        ck_console_scroll_reset();
        return;
    }
    ck_puts("scroll: usage: scroll [up|down|top|bottom]\n");
}

static void cmd_resolution(const char *args)
{
    u32 width = 0, height = 0;
    ck_display_get_resolution(&width, &height);

    if (!args || !*args) {
        ck_printk("resolution: current %ux%u (default 80x25)\n",
                  (unsigned int)width, (unsigned int)height);
        return;
    }

    char first[16];
    char second[16];
    size_t fi = 0;
    size_t si = 0;

    while (*args == ' ')
        args++;
    while (*args && *args != ' ' && *args != 'x' && *args != 'X') {
        if (fi + 1 >= sizeof(first)) {
            ck_puts("resolution: usage: resolution [WxH|W H]\n");
            return;
        }
        first[fi++] = *args++;
    }
    first[fi] = '\0';

    if (*args == 'x' || *args == 'X') {
        args++;
        while (*args && *args != ' ') {
            if (si + 1 >= sizeof(second)) {
                ck_puts("resolution: usage: resolution [WxH|W H]\n");
                return;
            }
            second[si++] = *args++;
        }
    } else {
        while (*args == ' ')
            args++;
        while (*args && *args != ' ') {
            if (si + 1 >= sizeof(second)) {
                ck_puts("resolution: usage: resolution [WxH|W H]\n");
                return;
            }
            second[si++] = *args++;
        }
    }
    second[si] = '\0';

    while (*args == ' ')
        args++;
    if (!first[0] || !second[0] || *args) {
        ck_puts("resolution: usage: resolution [WxH|W H]\n");
        return;
    }

    if (parse_u32_arg(first, &width) != 0 || parse_u32_arg(second, &height) != 0) {
        ck_puts("resolution: width and height must be positive integers\n");
        return;
    }

    if (ck_display_set_resolution(width, height) != 0) {
        ck_puts("resolution: invalid resolution (must be in range 1..4096)\n");
        return;
    }

    ck_display_get_resolution(&width, &height);
    ck_printk("resolution: set to %ux%u\n", (unsigned int)width, (unsigned int)height);
}

/* ── Command dispatcher ──────────────────────────────────────────────── */

static void shell_exec(char *line)
{
    /* Strip leading spaces */
    while (*line == ' ')
        line++;

    if (!*line)
        return;

    /* Split command from arguments at first space */
    char *args = line;
    while (*args && *args != ' ')
        args++;
    if (*args == ' ') {
        *args = '\0';
        args++;
        while (*args == ' ')
            args++;
    } else {
        args = NULL;
    }

    if      (strcmp(line, "help")      == 0) cmd_help();
    else if (strcmp(line, "clear")     == 0) cmd_clear();
    else if (strcmp(line, "uname")     == 0) cmd_uname();
    else if (strcmp(line, "fastfetch") == 0) cmd_fastfetch();
    else if (strcmp(line, "neofetch")  == 0) cmd_fastfetch();
    else if (strcmp(line, "free")      == 0) cmd_free();
    else if (strcmp(line, "mem")       == 0) cmd_mem();
    else if (strcmp(line, "ls")        == 0) cmd_ls(args);
    else if (strcmp(line, "cat")       == 0) cmd_cat(args);
    else if (strcmp(line, "stat")      == 0) cmd_stat(args);
    else if (strcmp(line, "touch")     == 0) cmd_touch(args);
    else if (strcmp(line, "write")     == 0) cmd_write(args);
    else if (strcmp(line, "cp")        == 0) cmd_cp(args);
    else if (strcmp(line, "mv")        == 0) cmd_mv(args);
    else if (strcmp(line, "rm")        == 0) cmd_rm(args);
    else if (strcmp(line, "tree")      == 0) cmd_tree(args);
    else if (strcmp(line, "du")        == 0) cmd_du(args);
    else if (strcmp(line, "mkdir")     == 0) cmd_mkdir(args);
    else if (strcmp(line, "rmdir")     == 0) cmd_rmdir(args);
    else if (strcmp(line, "hexdump")   == 0) cmd_hexdump(args);
    else if (strcmp(line, "cd")        == 0) cmd_cd(args);
    else if (strcmp(line, "pwd")       == 0) cmd_pwd();
    else if (strcmp(line, "history")   == 0) cmd_history();
    else if (strcmp(line, "uptime")    == 0) cmd_uptime();
    else if (strcmp(line, "whoami")    == 0) cmd_whoami();
    else if (strcmp(line, "hostname")  == 0) cmd_hostname();
    else if (strcmp(line, "ps")        == 0) cmd_ps();
    else if (strcmp(line, "tasks")     == 0) cmd_tasks();
    else if (strcmp(line, "sleep")     == 0) cmd_sleep(args);
    else if (strcmp(line, "reboot")    == 0) cmd_reboot();
    else if (strcmp(line, "halt")      == 0) cmd_halt();
    else if (strcmp(line, "echo")      == 0) cmd_echo(args);
    else if (strcmp(line, "setup")     == 0) cmd_setup();
    else if (strcmp(line, "setup-guide") == 0) cmd_setup_guide();
    else if (strcmp(line, "setup-alpine") == 0) cmd_installer_style();
    else if (strcmp(line, "arch-install") == 0) {
        ck_puts("arch-install: deprecated alias, running 'setup'\n");
        cmd_setup();
    }
    else if (strcmp(line, "sudo")      == 0) cmd_sudo(args);
    else if (strcmp(line, "netinfo")   == 0) cmd_netinfo();
    else if (strcmp(line, "ssh")       == 0) cmd_ssh();
    else if (strcmp(line, "motd")      == 0) cmd_motd();
    else if (strcmp(line, "mouse")     == 0) cmd_mouse();
    else if (strcmp(line, "palette")   == 0) cmd_palette();
    else if (strcmp(line, "tips")      == 0) cmd_tips();
    else if (strcmp(line, "syscheck")  == 0) cmd_syscheck();
    else if (strcmp(line, "scroll")    == 0) cmd_scroll(args);
    else if (strcmp(line, "resolution") == 0) cmd_resolution(args);
    else if (strcmp(line, "credits")   == 0) cmd_credits();
    else if (strcmp(line, "banner")    == 0) shell_print_banner();
    else if (strcmp(line, "kblayout")  == 0) cmd_kblayout(args);
    else if (strcmp(line, "layout")    == 0) cmd_kblayout(args);
    else
        ck_printk("cksh: %s: command not found\n", line);
}

/* ── Prompt ──────────────────────────────────────────────────────────── */

static void print_prompt(void)
{
    /* { cyan, user red, @ yellow, hostname green, space, cwd blue, } cyan, # white */
    ck_set_color(CK_COLOR_LIGHT_CYAN);
    ck_putchar('{');
    ck_set_color(CK_COLOR_LIGHT_RED);
    ck_puts(SHELL_USER);
    ck_set_color(CK_COLOR_YELLOW);
    ck_putchar('@');
    ck_set_color(CK_COLOR_LIGHT_GREEN);
    ck_puts(SHELL_HOSTNAME);
    ck_reset_color();
    ck_putchar(' ');
    ck_set_color(CK_COLOR_LIGHT_BLUE);
    ck_puts(shell_cwd);
    ck_set_color(CK_COLOR_LIGHT_CYAN);
    ck_putchar('}');
    ck_reset_color();
    ck_puts(" # ");
}

/* ── Shell task entry point ──────────────────────────────────────────── */

static char shell_line[SHELL_LINE_MAX];
static int  shell_pos = 0;

void task_shell(void *arg)
{
    (void)arg;

    /* Yield once so boot log flushes without blocking on PIT tick timing. */
    sched_yield();

    shell_print_banner();
    ck_set_color(CK_COLOR_LIGHT_GREEN);
    ck_puts("Welcome to ComputeKERNEL");
    ck_reset_color();
    ck_puts("  Type 'help' for commands, 'tips' for shortcuts, 'motd' for status.\n\n");
    print_prompt();

    for (;;) {
        int c = keyboard_getchar();
        if (!c) {
            sched_yield();
            continue;
        }

        if (c == '\b') {
            /* Backspace: erase last character */
            if (shell_pos > 0) {
                shell_pos--;
                ck_putchar('\b');
            }
        } else if (c == '\n' || c == '\r') {
            ck_putchar('\n');
            shell_line[shell_pos] = '\0';
            hist_add(shell_line);
            shell_hist_nav = -1;
            shell_exec(shell_line);
            shell_pos = 0;
            print_prompt();
        } else if (c == KEY_CTRL_C) {
            ck_puts("^C\n");
            shell_pos = 0;
            shell_line[0] = '\0';
            shell_hist_nav = -1;
            print_prompt();
        } else if (c == KEY_CTRL_X) {
            if (shell_pos > 0) {
                memcpy(shell_clipboard, shell_line, (size_t)shell_pos);
            }
            shell_clipboard[shell_pos] = '\0';
            while (shell_pos > 0) {
                shell_pos--;
                ck_putchar('\b');
            }
            shell_line[0] = '\0';
            shell_hist_nav = -1;
        } else if (c == KEY_CTRL_V) {
            int i = 0;
            while (shell_clipboard[i] && shell_pos < SHELL_LINE_MAX - 1) {
                shell_line[shell_pos++] = shell_clipboard[i];
                ck_putchar(shell_clipboard[i]);
                i++;
            }
        } else if (c == KEY_UP) {
            /* Navigate to older history entry */
            int next_nav = (shell_hist_nav == -1) ? 0 : shell_hist_nav + 1;
            if (next_nav < shell_hist_count) {
                if (shell_hist_nav == -1) {
                    /* Save the line being typed before navigating */
                    strncpy(shell_line_saved, shell_line, SHELL_LINE_MAX - 1);
                    shell_line_saved[SHELL_LINE_MAX - 1] = '\0';
                }
                shell_hist_nav = next_nav;
                /* Clear current line on screen */
                while (shell_pos > 0) {
                    shell_pos--;
                    ck_putchar('\b');
                }
                /* Load and display history entry */
                const char *entry = hist_get(shell_hist_nav);
                if (entry) {
                    strncpy(shell_line, entry, SHELL_LINE_MAX - 1);
                    shell_line[SHELL_LINE_MAX - 1] = '\0';
                    shell_pos = (int)strlen(shell_line);
                    ck_puts(shell_line);
                }
            }
        } else if (c == KEY_DOWN) {
            /* Navigate to newer history entry (or restore saved line) */
            if (shell_hist_nav > 0) {
                shell_hist_nav--;
                /* Clear current line on screen */
                while (shell_pos > 0) {
                    shell_pos--;
                    ck_putchar('\b');
                }
                const char *entry = hist_get(shell_hist_nav);
                if (entry) {
                    strncpy(shell_line, entry, SHELL_LINE_MAX - 1);
                    shell_line[SHELL_LINE_MAX - 1] = '\0';
                    shell_pos = (int)strlen(shell_line);
                    ck_puts(shell_line);
                }
            } else if (shell_hist_nav == 0) {
                /* Restore line that was being typed */
                shell_hist_nav = -1;
                while (shell_pos > 0) {
                    shell_pos--;
                    ck_putchar('\b');
                }
                strncpy(shell_line, shell_line_saved, SHELL_LINE_MAX - 1);
                shell_line[SHELL_LINE_MAX - 1] = '\0';
                shell_pos = (int)strlen(shell_line);
                ck_puts(shell_line);
            }
        } else if (c == KEY_PGUP) {
            ck_console_scroll_up(1);
        } else if (c == KEY_PGDN) {
            ck_console_scroll_down(1);
        } else if ((unsigned char)c >= ASCII_SPACE && (unsigned char)c < ASCII_DEL) {
            if (shell_pos < SHELL_LINE_MAX - 1) {
                /* Any printable character cancels history navigation */
                if (shell_hist_nav != -1)
                    shell_hist_nav = -1;
                ck_console_scroll_reset();
                shell_line[shell_pos++] = (char)c;
                ck_putchar(c);
            }
        }
    }
}
