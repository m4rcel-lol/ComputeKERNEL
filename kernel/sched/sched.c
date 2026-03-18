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

static struct task tasks[MAX_TASKS];
static int         num_tasks  = 0;
static int         current_tid = 0;    /* index into tasks[] */

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
    memset(tasks, 0, sizeof(tasks));
    num_tasks   = 0;
    current_tid = 0;

    /* Create the idle task (tid = 0) */
    task_create("idle", idle_task, NULL, DEFAULT_STACK_SIZE);
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

    /*
     * Set up an initial stack frame so that when switch_to() first
     * 'returns' into this task, it lands on task_entry() which then
     * calls fn(arg).
     *
     * Stack layout (from top, 8 bytes each):
     *   [rsp+0]  = address of task_entry (popped as return address by ret)
     *   callee-saved regs (6 × 8 = 48 bytes) set to 0/arg
     *
     * switch_to() pops: rbp, rbx, r12, r13, r14, r15 then ret.
     */
    u64 *sp = (u64 *)(uintptr_t)t->stack_top;

    /* Trampoline: called when fn() returns */
    *--sp = (u64)(uintptr_t)task_exit;

    /* Fake "return address" that switch_to will pop via ret */
    /* We need a small trampoline that calls fn(arg) */
    /* We'll encode it by pushing rbp=0, rbx=fn, r12=arg, then
       a ret address pointing to a trampoline.
       Simpler: use a dedicated inline trampoline stored per-task. */

    /* Actual approach: we push a fake call frame.
     * switch_to pops r15, r14, r13, r12, rbx, rbp then executes ret.
     * We want ret to jump to a trampoline that calls fn(arg).
     * So the ret address (top of frame) must be that trampoline.
     * We pass fn in r12 and arg in r13.
     */

    /* ret address → task_trampoline */
    extern void task_trampoline(void);
    *--sp = (u64)(uintptr_t)task_trampoline;
    /* r15 */ *--sp = 0;
    /* r14 */ *--sp = 0;
    /* r13 */ *--sp = (u64)(uintptr_t)arg;    /* task argument */
    /* r12 */ *--sp = (u64)(uintptr_t)fn;     /* task function */
    /* rbx */ *--sp = 0;
    /* rbp */ *--sp = 0;

    t->rsp = (u64)(uintptr_t)sp;

    ck_printk("[sched] created task '%s' (tid=%d)\n", t->name, tid);
    return tid;
}

/* ── Round-robin selection ──────────────────────────────────────────── */
static int pick_next(void)
{
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
    if (num_tasks <= 1) return;

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

/* Called from the PIT IRQ0 handler */
void sched_tick(void)
{
    if (num_tasks <= 1) return;
    tasks[current_tid].ticks++;
    sched_yield();
}

struct task *sched_current(void)
{
    return &tasks[current_tid];
}
