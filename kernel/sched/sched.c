/*
 * kernel/sched/sched.c
 *
 * Priority-aware round-robin kernel-thread scheduler.
 *
 * Scheduling policy:
 *   - Tasks are grouped into four priority levels (HIGH, NORMAL, LOW, IDLE).
 *   - The scheduler always picks the highest-priority non-blocked task.
 *   - Within a priority level, tasks are served round-robin.
 *   - Sleeping tasks (TASK_BLOCKED with sleep_until > 0) are woken when
 *     the tick counter reaches their deadline.
 *
 * Each task runs in ring 0 with its own heap-allocated stack.
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

/* PIT frequency – must match pit.c */
#define SCHED_HZ 100UL

/* Low-level assembly helpers (boot/x86_64/switch.S) */
extern void switch_to(struct task *prev, struct task *next);
extern void switch_to_first(struct task *next);
extern void task_trampoline(void);

/* Tick counter (read from pit.c) */
extern u64 pit_get_ticks(void);

static struct task tasks[MAX_TASKS];
static int         num_tasks   = 0;
static int         current_tid = 0;
static int         sched_started = 0;

/* ── Task exit ──────────────────────────────────────────────────────── */

void task_exit(void)
{
    tasks[current_tid].state = TASK_DEAD;
    ck_printk("[sched] task '%s' (tid=%d) exited after %llu ticks\n",
              tasks[current_tid].name, current_tid, tasks[current_tid].ticks);
    for (;;) sched_yield();
}

/* ── Idle task ──────────────────────────────────────────────────────── */

static void idle_task(void *arg)
{
    (void)arg;
    for (;;)
        __asm__ __volatile__("sti; hlt" ::: "memory");
}

/* ── Scheduler init ─────────────────────────────────────────────────── */

void sched_init(void)
{
    u64 flags = irq_save();
    memset(tasks, 0, sizeof(tasks));
    num_tasks    = 0;
    current_tid  = 0;
    sched_started = 0;
    /* Idle task at lowest priority */
    task_create_prio("idle", idle_task, NULL, DEFAULT_STACK_SIZE, TASK_PRIO_IDLE);
    irq_restore(flags);
}

/* ── Task creation ──────────────────────────────────────────────────── */

int task_create(const char *name, task_func_t fn, void *arg, size_t stack_size)
{
    return task_create_prio(name, fn, arg, stack_size, TASK_PRIO_NORMAL);
}

int task_create_prio(const char *name, task_func_t fn, void *arg,
                     size_t stack_size, int priority)
{
    if (num_tasks >= MAX_TASKS)
        return -1;
    if (stack_size == 0)
        stack_size = DEFAULT_STACK_SIZE;
    if (priority < TASK_PRIO_HIGH || priority > TASK_PRIO_IDLE)
        priority = TASK_PRIO_NORMAL;

    u8 *stack = (u8 *)kmalloc(stack_size);
    if (!stack) {
        ck_puts("[sched] task_create: out of memory\n");
        return -1;
    }
    memset(stack, 0, stack_size);

    int tid = num_tasks++;
    struct task *t = &tasks[tid];
    t->tid         = tid;
    t->state       = TASK_READY;
    t->priority    = priority;
    t->ticks       = 0;
    t->sleep_until = 0;
    t->stack_size  = stack_size;
    t->stack_top   = (u64)(uintptr_t)(stack + stack_size);
    strncpy(t->name, name, TASK_NAME_LEN - 1);
    t->name[TASK_NAME_LEN - 1] = '\0';

    /*
     * Build the initial stack frame for switch_to() / switch_to_first().
     * switch_to saves/restores: rbp, rbx, r12, r13, r14, r15 (in that order).
     * After restoring, it executes 'ret' which pops the return address.
     *
     * Stack layout (high → low, i.e. push order):
     *   task_exit        ← guard if trampoline ever returns
     *   task_trampoline  ← 'ret' lands here
     *   rbp = 0
     *   rbx = 0
     *   r12 = fn         ← task function
     *   r13 = arg        ← task argument
     *   r14 = 0
     *   r15 = 0          ← t->rsp points here
     */
    u64 *sp = (u64 *)(uintptr_t)t->stack_top;
    *--sp = (u64)(uintptr_t)task_exit;
    *--sp = (u64)(uintptr_t)task_trampoline;
    *--sp = 0;                          /* rbp */
    *--sp = 0;                          /* rbx */
    *--sp = (u64)(uintptr_t)fn;         /* r12 */
    *--sp = (u64)(uintptr_t)arg;        /* r13 */
    *--sp = 0;                          /* r14 */
    *--sp = 0;                          /* r15 */
    t->rsp = (u64)(uintptr_t)sp;

    ck_printk("[sched] created task '%s' (tid=%d, prio=%d)\n",
              t->name, tid, priority);
    return tid;
}

/* ── Scheduler: pick next runnable task ─────────────────────────────── */

/*
 * Priority-aware round-robin: scan from the highest priority level down.
 * Within each level, continue from where we left off (round-robin).
 */
static int pick_next(void)
{
    if (num_tasks <= 0)
        return 0;

    /* Try each priority level from highest to lowest */
    for (int prio = TASK_PRIO_HIGH; prio <= TASK_PRIO_IDLE; prio++) {
        /* Start scanning after the current task */
        int start = (current_tid + 1) % num_tasks;
        int i     = start;
        do {
            struct task *t = &tasks[i];
            if (t->priority == prio &&
                (t->state == TASK_READY || t->state == TASK_RUNNING))
                return i;
            i = (i + 1) % num_tasks;
        } while (i != start);

        /* Also check the current task at this priority level */
        if (tasks[current_tid].priority == prio &&
            (tasks[current_tid].state == TASK_READY ||
             tasks[current_tid].state == TASK_RUNNING))
            return current_tid;
    }

    /* Fallback: idle task (tid 0) */
    return 0;
}

/* ── Wake sleeping tasks ────────────────────────────────────────────── */

static void wake_sleepers(void)
{
    u64 now = pit_get_ticks();
    for (int i = 0; i < num_tasks; i++) {
        struct task *t = &tasks[i];
        if (t->state == TASK_BLOCKED && t->sleep_until > 0 &&
            now >= t->sleep_until) {
            t->state       = TASK_READY;
            t->sleep_until = 0;
        }
    }
}

/* ── Public API ─────────────────────────────────────────────────────── */

void sched_yield(void)
{
    if (num_tasks <= 1 || !sched_started)
        return;

    wake_sleepers();

    int prev_tid = current_tid;
    int next_tid = pick_next();

    if (prev_tid == next_tid)
        return;

    if (tasks[prev_tid].state == TASK_RUNNING)
        tasks[prev_tid].state = TASK_READY;
    tasks[next_tid].state = TASK_RUNNING;
    current_tid           = next_tid;

    switch_to(&tasks[prev_tid], &tasks[next_tid]);
}

void sched_sleep_ms(u64 ms)
{
    if (ms == 0) {
        sched_yield();
        return;
    }
    u64 ticks_needed = (ms * SCHED_HZ + 999) / 1000;
    tasks[current_tid].sleep_until = pit_get_ticks() + ticks_needed;
    tasks[current_tid].state       = TASK_BLOCKED;
    sched_yield();
}

void sched_tick(void)
{
    if (num_tasks <= 1 || !sched_started)
        return;
    tasks[current_tid].ticks++;
    /* Wake any sleeping tasks before deciding to preempt */
    wake_sleepers();
    /* Only preempt NORMAL/LOW/IDLE tasks; HIGH priority tasks run to yield */
    if (tasks[current_tid].priority >= TASK_PRIO_NORMAL)
        sched_yield();
}

void sched_start(void)
{
    if (num_tasks <= 0)
        for (;;) __asm__ __volatile__("cli; hlt");

    int next_tid = pick_next();
    if (next_tid < 0 || next_tid >= num_tasks)
        next_tid = 0;

    tasks[next_tid].state = TASK_RUNNING;
    current_tid  = next_tid;
    sched_started = 1;

    switch_to_first(&tasks[next_tid]);
    __builtin_unreachable();
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
