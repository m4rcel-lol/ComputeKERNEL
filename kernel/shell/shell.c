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
    ck_puts("  uptime            show system uptime in seconds\n");
    ck_puts("  halt              halt the system\n");
}

static void cmd_clear(void)
{
    /* Emit 25 newlines to scroll the entire screen off */
    for (int i = 0; i < 25; i++)
        ck_putchar('\n');
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
    ck_puts("   .---.           " SHELL_USER "@" SHELL_HOSTNAME "\n");
    ck_puts("  /     \\          " "-----------------------------\n");
    ck_puts(" | () () |         " "OS:      ComputeKERNEL 1.0.0\n");
    ck_puts("  \\  ^  /          " "Arch:    x86_64\n");
    ck_puts("   '---'           " "Kernel:  ComputeKERNEL v1.0.0\n");
    ck_puts("                   " "Shell:   cksh 1.0.0\n");
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

static void cmd_uptime(void)
{
    u64 ticks  = pit_get_ticks();
    u64 secs   = ticks / PIT_HZ;
    u64 mins   = secs / 60;
    u64 hrs    = mins / 60;
    ck_printk("up %llu:%02llu:%02llu  (%llu ticks)\n",
              hrs, mins % 60, secs % 60, ticks);
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
    else if (strcmp(line, "uptime")    == 0) cmd_uptime();
    else if (strcmp(line, "halt")      == 0) cmd_halt();
    else if (strcmp(line, "echo")      == 0) cmd_echo(args);
    else
        ck_printk("cksh: %s: command not found\n", line);
}

/* ── Prompt ──────────────────────────────────────────────────────────── */

static void print_prompt(void)
{
    ck_puts("{" SHELL_USER "@" SHELL_HOSTNAME "} # ");
}

/* ── Shell task entry point ──────────────────────────────────────────── */

static char shell_line[SHELL_LINE_MAX];
static int  shell_pos = 0;

void task_shell(void *arg)
{
    (void)arg;

    /* Wait briefly so the boot log finishes printing */
    pit_sleep_ms(200);

    ck_puts("\nWelcome to ComputeKERNEL!  Type 'help' for a list of commands.\n\n");
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
