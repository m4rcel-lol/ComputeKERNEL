/*
 * kernel/sched/sched.c
 *
 * Round-robin kernel-thread scheduler.
 *
 * Each task runs in ring 0 with its own 16 KiB stack.
 * When a task function returns, the task is marked DEAD.
 * The scheduler never returns to kmain once started.
 */
#include <ck/sched.h>
#include <ck/mm.h>
#include <ck/kernel.h>
#include <ck/string.h>
#include <ck/types.h>
#include <ck/io.h>

#define DEFAULT_STACK_SIZE (16 * 1024)

/* Low-level assembly helpers (boot/x86_64/switch.S) */
extern void switch_to(struct task *prev, struct task *next);
extern void switch_to_first(struct task *next);
extern void task_trampoline(void);

static struct task tasks[MAX_TASKS];
static int         num_tasks  = 0;
static int         current_tid = 0;    /* index into tasks[] */
static int         sched_started = 0;

/* Task exit function (non-static so switch.S can reference it) */
void task_exit(void)
{
    /* Mark current task dead and yield (never returns) */
    tasks[current_tid].state = TASK_DEAD;
    ck_printk("[sched] task '%s' exited\n", tasks[current_tid].name);
    for (;;) sched_yield();
}

/* ── Idle task ──────────────────────────────────────────────────────── */
static void idle_task(void *arg)
{
    (void)arg;
    for (;;) {
        __asm__ __volatile__("sti; hlt" ::: "memory");
    }
}

/* ── Public API ─────────────────────────────────────────────────────── */

void sched_init(void)
{
    u64 flags = irq_save();

    memset(tasks, 0, sizeof(tasks));
    num_tasks   = 0;
    current_tid = 0;
    sched_started = 0;

    /* Create the idle task (tid = 0) */
    task_create("idle", idle_task, NULL, DEFAULT_STACK_SIZE);

    irq_restore(flags);
}

int task_create(const char *name, task_func_t fn, void *arg, size_t stack_size)
{
    if (num_tasks >= MAX_TASKS)
        return -1;

    if (stack_size == 0)
        stack_size = DEFAULT_STACK_SIZE;

    u8 *stack = (u8 *)kmalloc(stack_size);
    if (!stack) {
        ck_puts("[sched] task_create: out of memory\n");
        return -1;
    }
    memset(stack, 0, stack_size);

    int tid = num_tasks++;
    struct task *t = &tasks[tid];
    t->tid        = tid;
    t->state      = TASK_READY;
    t->ticks      = 0;
    t->stack_size = stack_size;
    t->stack_top  = (u64)(uintptr_t)(stack + stack_size);
    strncpy(t->name, name, TASK_NAME_LEN - 1);

    u64 *sp = (u64 *)(uintptr_t)t->stack_top;

    /*
     * Build the initial stack frame so that switch_to() / switch_to_first()
     * "returns" into task_trampoline, which then calls fn(arg).
     *
     * switch_to saves/restores in this order:
     *   push rbp, rbx, r12, r13, r14, r15  (save)
     *   pop  r15, r14, r13, r12, rbx, rbp  (restore)
     *   ret
     *
     * So at t->rsp the memory layout must be (low → high):
     *   [rsp+ 0] r15 = 0
     *   [rsp+ 8] r14 = 0
     *   [rsp+16] r13 = arg   (task_trampoline reads r13 as the argument)
     *   [rsp+24] r12 = fn    (task_trampoline calls *r12 as the function)
     *   [rsp+32] rbx = 0
     *   [rsp+40] rbp = 0
     *   [rsp+48] task_trampoline  ← popped by ret
     *   [rsp+56] task_exit        ← guard if stack ever unwinds further
     *
     * We push from high address to low (each *--sp decrements sp first).
     */
    *--sp = (u64)(uintptr_t)task_exit;       /* guard                */
    *--sp = (u64)(uintptr_t)task_trampoline; /* return address → ret */
    /* rbp */ *--sp = 0;
    /* rbx */ *--sp = 0;
    /* r12 */ *--sp = (u64)(uintptr_t)fn;    /* task function        */
    /* r13 */ *--sp = (u64)(uintptr_t)arg;   /* task argument        */
    /* r14 */ *--sp = 0;
    /* r15 */ *--sp = 0;

    t->rsp = (u64)(uintptr_t)sp;

    ck_printk("[sched] created task '%s' (tid=%d)\n", t->name, tid);
    return tid;
}

/* ── Round-robin selection ──────────────────────────────────────────── */
static int pick_next(void)
{
    if (num_tasks <= 0)
        return 0;

    int start = (current_tid + 1) % num_tasks;
    int i     = start;
    do {
        if (tasks[i].state == TASK_READY || tasks[i].state == TASK_RUNNING)
            return i;
        i = (i + 1) % num_tasks;
    } while (i != start);

    /* Fall back to idle (tid 0) */
    return 0;
}

void sched_yield(void)
{
    if (num_tasks <= 1 || !sched_started) return;

    int prev_tid = current_tid;
    int next_tid = pick_next();

    if (prev_tid == next_tid) return;

    /* Only transition to READY if currently running (not dead/blocked) */
    if (tasks[prev_tid].state == TASK_RUNNING)
        tasks[prev_tid].state = TASK_READY;
    tasks[next_tid].state = TASK_RUNNING;
    current_tid           = next_tid;

    switch_to(&tasks[prev_tid], &tasks[next_tid]);
}

void sched_start(void)
{
    if (num_tasks <= 0)
        for (;;) __asm__ __volatile__("cli; hlt");

    int next_tid = pick_next();
    if (next_tid < 0 || next_tid >= num_tasks)
        next_tid = 0;

    tasks[next_tid].state = TASK_RUNNING;
    current_tid = next_tid;
    sched_started = 1;

    switch_to_first(&tasks[next_tid]);
    __builtin_unreachable();
}

/* Called from the PIT IRQ0 handler */
void sched_tick(void)
{
    if (num_tasks <= 1 || !sched_started) return;
    tasks[current_tid].ticks++;
    sched_yield();
}

struct task *sched_current(void)
{
    return &tasks[current_tid];
}

int sched_num_tasks(void)
{
    return num_tasks;
}

struct task *sched_get_task(int idx)
{
    if (idx < 0 || idx >= num_tasks)
        return NULL;
    return &tasks[idx];
}
