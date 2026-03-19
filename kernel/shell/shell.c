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

/* ── Built-in commands ───────────────────────────────────────────────── */

static void cmd_help(void)
{
    ck_puts("cksh built-in commands:\n");
    ck_puts("  help              show this help message\n");
    ck_puts("  clear             clear the screen\n");
    ck_puts("  echo [text]       print text to the screen\n");
    ck_puts("  uname             print kernel/OS information\n");
    ck_puts("  fastfetch         display system information\n");
    ck_puts("  free              show free memory pages\n");
    ck_puts("  ls [path]         list directory contents\n");
    ck_puts("  cat <path>        print file contents\n");
    ck_puts("  touch <path>      create an empty file\n");
    ck_puts("  uptime            show system uptime\n");
    ck_puts("  whoami            print current username\n");
    ck_puts("  hostname          print system hostname\n");
    ck_puts("  ps                list running tasks\n");
    ck_puts("  sleep <n>         sleep for n seconds\n");
    ck_puts("  pwd               print working directory\n");
    ck_puts("  reboot            reboot the system\n");
    ck_puts("  halt              halt the system\n");
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

static void cmd_ls(const char *path)
{
    if (!path || !*path)
        path = "/";

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        ck_printk("ls: %s: No such file or directory\n", path);
        return;
    }

    char name[VFS_NAME_MAX];
    int found = 0;
    for (u32 i = 0; vfs_readdir(fd, i, name) == 0; i++) {
        ck_printk("  %s\n", name);
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

    int fd = vfs_open(path, O_RDONLY);
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

static void cmd_touch(const char *path)
{
    if (!path || !*path) {
        ck_puts("touch: missing file operand\n");
        return;
    }

    int fd = vfs_open(path, O_CREAT | O_WRONLY);
    if (fd < 0) {
        ck_printk("touch: %s: cannot create file\n", path);
        return;
    }
    vfs_close(fd);
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
    ck_puts("/\n");
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
    else if (strcmp(line, "ls")        == 0) cmd_ls(args);
    else if (strcmp(line, "cat")       == 0) cmd_cat(args);
    else if (strcmp(line, "touch")     == 0) cmd_touch(args);
    else if (strcmp(line, "uptime")    == 0) cmd_uptime();
    else if (strcmp(line, "whoami")    == 0) cmd_whoami();
    else if (strcmp(line, "hostname")  == 0) cmd_hostname();
    else if (strcmp(line, "ps")        == 0) cmd_ps();
    else if (strcmp(line, "sleep")     == 0) cmd_sleep(args);
    else if (strcmp(line, "pwd")       == 0) cmd_pwd();
    else if (strcmp(line, "reboot")    == 0) cmd_reboot();
    else if (strcmp(line, "halt")      == 0) cmd_halt();
    else if (strcmp(line, "echo")      == 0) cmd_echo(args);
    else
        ck_printk("cksh: %s: command not found\n", line);
}

/* ── Prompt ──────────────────────────────────────────────────────────── */

static void print_prompt(void)
{
    /* { cyan, user red, @ yellow, hostname green, } cyan, # white */
    ck_set_color(CK_COLOR_LIGHT_CYAN);
    ck_putchar('{');
    ck_set_color(CK_COLOR_LIGHT_RED);
    ck_puts(SHELL_USER);
    ck_set_color(CK_COLOR_YELLOW);
    ck_putchar('@');
    ck_set_color(CK_COLOR_LIGHT_GREEN);
    ck_puts(SHELL_HOSTNAME);
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

