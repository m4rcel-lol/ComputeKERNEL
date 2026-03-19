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

/* Provided by pit.c / keyboard.c */
extern int  keyboard_getchar(void);
extern u64  pit_get_ticks(void);
extern void pit_sleep_ms(u64 ms);
extern u64  pmm_free_pages(void);

#define SHELL_LINE_MAX  256
#define SHELL_HOSTNAME  "computekernel"
#define SHELL_USER      "user"

/* Printable ASCII range */
#define ASCII_SPACE 0x20
#define ASCII_DEL   0x7f

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

static char shell_hist[SHELL_HIST_MAX][SHELL_HIST_LEN];
static int  shell_hist_count = 0;
static int  shell_hist_head  = 0;   /* next write slot */

/* ── Current working directory ───────────────────────────────────────── */
static char shell_cwd[VFS_NAME_MAX] = "/";

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

/* ── Path helper ─────────────────────────────────────────────────────── */

/* Resolve 'arg' relative to shell_cwd into 'out' (VFS_NAME_MAX bytes). */
static void path_resolve(const char *arg, char *out)
{
    if (!arg || !*arg) {
        strncpy(out, shell_cwd, VFS_NAME_MAX - 1);
        out[VFS_NAME_MAX - 1] = '\0';
        return;
    }
    if (arg[0] == '/') {
        strncpy(out, arg, VFS_NAME_MAX - 1);
        out[VFS_NAME_MAX - 1] = '\0';
        return;
    }
    /* Relative path: prepend CWD */
    size_t cwdlen = strlen(shell_cwd);
    size_t arglen = strlen(arg);
    if (cwdlen + 1 + arglen >= VFS_NAME_MAX) {
        /* fallback: use CWD unchanged */
        strncpy(out, shell_cwd, VFS_NAME_MAX - 1);
        out[VFS_NAME_MAX - 1] = '\0';
        return;
    }
    memcpy(out, shell_cwd, cwdlen);
    if (out[cwdlen - 1] != '/') {
        out[cwdlen++] = '/';
    }
    memcpy(out + cwdlen, arg, arglen + 1);
}

/* ── Built-in commands ───────────────────────────────────────────────── */

static void cmd_help(void)
{
    ck_puts("cksh built-in commands:\n");
    ck_puts("  help                  show this help message\n");
    ck_puts("  clear                 clear the screen\n");
    ck_puts("  echo [text]           print text to the screen\n");
    ck_puts("  uname                 print kernel/OS information\n");
    ck_puts("  fastfetch             display system information\n");
    ck_puts("  free                  show free memory pages\n");
    ck_puts("  mem                   show detailed memory statistics\n");
    ck_puts("  ls [path]             list directory contents\n");
    ck_puts("  cat <path>            print file contents\n");
    ck_puts("  stat <path>           show file or directory metadata\n");
    ck_puts("  touch <path>          create an empty file\n");
    ck_puts("  write <path> <text>   write text to a file (overwrites)\n");
    ck_puts("  cp <src> <dst>        copy a file\n");
    ck_puts("  mv <src> <dst>        move/rename a file or directory\n");
    ck_puts("  rm <path>             remove a file\n");
    ck_puts("  mkdir <path>          create a directory\n");
    ck_puts("  rmdir <path>          remove an empty directory\n");
    ck_puts("  hexdump <path>        hex dump file contents\n");
    ck_puts("  cd [path]             change working directory\n");
    ck_puts("  pwd                   print working directory\n");
    ck_puts("  history               show command history\n");
    ck_puts("  uptime                show system uptime\n");
    ck_puts("  whoami                print current username\n");
    ck_puts("  hostname              print system hostname\n");
    ck_puts("  ps                    list running tasks\n");
    ck_puts("  sleep <n>             sleep for n seconds\n");
    ck_puts("  reboot                reboot the system\n");
    ck_puts("  halt                  halt the system\n");
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
    u64 free_pages = pmm_free_pages();
    u64 ticks      = pit_get_ticks();
    u64 uptime_s   = ticks / PIT_HZ;

    ck_puts("\n");
    ck_set_color(CK_COLOR_LIGHT_GREEN);
    ck_puts("   .---.           ");
    ck_set_color(CK_COLOR_LIGHT_RED);
    ck_puts(SHELL_USER);
    ck_set_color(CK_COLOR_YELLOW);
    ck_puts("@");
    ck_set_color(CK_COLOR_LIGHT_GREEN);
    ck_puts(SHELL_HOSTNAME);
    ck_reset_color();
    ck_puts("\n");

    ck_set_color(CK_COLOR_LIGHT_GREEN);
    ck_puts("  /     \\          ");
    ck_reset_color();
    ck_puts("-----------------------------\n");

    ck_set_color(CK_COLOR_LIGHT_GREEN);
    ck_puts(" | () () |         ");
    ck_reset_color();
    ck_puts("OS:      ComputeKERNEL 1.0.0\n");

    ck_set_color(CK_COLOR_LIGHT_GREEN);
    ck_puts("  \\  ^  /          ");
    ck_reset_color();
    ck_puts("Arch:    x86_64\n");

    ck_set_color(CK_COLOR_LIGHT_GREEN);
    ck_puts("   '---'           ");
    ck_reset_color();
    ck_puts("Kernel:  ComputeKERNEL v1.0.0\n");

    ck_puts("                   Shell:   cksh 1.0.0\n");
    ck_printk("                   Uptime:  %llu seconds\n", uptime_s);
    ck_printk("                   Memory:  %llu pages free (%llu KiB)\n",
              free_pages, free_pages * 4ULL);
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
    path_resolve(path, resolved);

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
    path_resolve(path, resolved);

    int fd = vfs_open(resolved, O_RDONLY);
    if (fd < 0) {
        ck_printk("cat: %s: No such file or directory\n", path);
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
    path_resolve(path, resolved);

    struct vfs_node *node = vfs_lookup(resolved);
    if (!node) {
        ck_printk("stat: %s: No such file or directory\n", path);
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
    path_resolve(path, resolved);

    int fd = vfs_open(resolved, O_CREAT | O_WRONLY);
    if (fd < 0) {
        ck_printk("touch: %s: cannot create file\n", path);
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
    path_resolve(path, resolved);

    int fd = vfs_open(resolved, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        ck_printk("write: %s: cannot open file\n", path);
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
    path_resolve(src, src_res);
    path_resolve(dst_arg, dst_res);

    int in = vfs_open(src_res, O_RDONLY);
    if (in < 0) {
        ck_printk("cp: %s: No such file or directory\n", src);
        return;
    }

    int out = vfs_open(dst_res, O_WRONLY | O_CREAT | O_TRUNC);
    if (out < 0) {
        vfs_close(in);
        ck_printk("cp: %s: cannot create\n", dst_arg);
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
    path_resolve(src, src_res);
    path_resolve(dst_arg, dst_res);

    if (vfs_rename(src_res, dst_res) < 0)
        ck_printk("mv: cannot rename '%s' to '%s'\n", src, dst_arg);
}

static void cmd_rm(const char *path)
{
    if (!path || !*path) {
        ck_puts("rm: missing operand\n");
        return;
    }

    char resolved[VFS_NAME_MAX];
    path_resolve(path, resolved);

    struct vfs_node *node = vfs_lookup(resolved);
    if (!node) {
        ck_printk("rm: %s: No such file or directory\n", path);
        return;
    }
    if (node->type == VFS_DIR) {
        ck_printk("rm: %s: is a directory (use rmdir)\n", path);
        return;
    }
    if (vfs_unlink(resolved) < 0)
        ck_printk("rm: %s: cannot remove\n", path);
}

static void cmd_mkdir(const char *path)
{
    if (!path || !*path) {
        ck_puts("mkdir: missing operand\n");
        return;
    }

    char resolved[VFS_NAME_MAX];
    path_resolve(path, resolved);

    if (vfs_mkdir(resolved) < 0)
        ck_printk("mkdir: %s: cannot create directory\n", path);
}

static void cmd_rmdir(const char *path)
{
    if (!path || !*path) {
        ck_puts("rmdir: missing operand\n");
        return;
    }

    char resolved[VFS_NAME_MAX];
    path_resolve(path, resolved);

    struct vfs_node *node = vfs_lookup(resolved);
    if (!node) {
        ck_printk("rmdir: %s: No such file or directory\n", path);
        return;
    }
    if (node->type != VFS_DIR) {
        ck_printk("rmdir: %s: Not a directory\n", path);
        return;
    }
    if (vfs_unlink(resolved) < 0)
        ck_printk("rmdir: %s: cannot remove (may not be empty)\n", path);
}

static void cmd_hexdump(const char *path)
{
    if (!path || !*path) {
        ck_puts("hexdump: missing file operand\n");
        return;
    }

    char resolved[VFS_NAME_MAX];
    path_resolve(path, resolved);

    int fd = vfs_open(resolved, O_RDONLY);
    if (fd < 0) {
        ck_printk("hexdump: %s: No such file or directory\n", path);
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

static void cmd_cd(const char *path)
{
    char resolved[VFS_NAME_MAX];

    if (!path || !*path) {
        /* cd with no args: go to root */
        shell_cwd[0] = '/'; shell_cwd[1] = '\0';
        return;
    }
    path_resolve(path, resolved);

    struct vfs_node *node = vfs_lookup(resolved);
    if (!node) {
        ck_printk("cd: %s: No such file or directory\n", path);
        return;
    }
    if (node->type != VFS_DIR) {
        ck_printk("cd: %s: Not a directory\n", path);
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

static void cmd_echo(const char *rest)
{
    if (rest && *rest)
        ck_printk("%s\n", rest);
    else
        ck_putchar('\n');
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
    else if (strcmp(line, "sleep")     == 0) cmd_sleep(args);
    else if (strcmp(line, "reboot")    == 0) cmd_reboot();
    else if (strcmp(line, "halt")      == 0) cmd_halt();
    else if (strcmp(line, "echo")      == 0) cmd_echo(args);
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

    /* Wait briefly so the boot log finishes printing */
    pit_sleep_ms(200);

    ck_set_color(CK_COLOR_LIGHT_GREEN);
    ck_puts("\nWelcome to ComputeKERNEL!");
    ck_reset_color();
    ck_puts("  Type 'help' for a list of commands.\n\n");
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
            shell_exec(shell_line);
            shell_pos = 0;
            print_prompt();
        } else if ((unsigned char)c >= ASCII_SPACE && (unsigned char)c < ASCII_DEL) {
            if (shell_pos < SHELL_LINE_MAX - 1) {
                shell_line[shell_pos++] = (char)c;
                ck_putchar(c);
            }
        }
    }
}

