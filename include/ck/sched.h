#ifndef CK_SCHED_H
#define CK_SCHED_H

#include <ck/types.h>

#define TASK_NAME_LEN  32
#define MAX_TASKS      64

/* Task states */
#define TASK_RUNNING   0
#define TASK_READY     1
#define TASK_BLOCKED   2
#define TASK_DEAD      3

typedef void (*task_func_t)(void *arg);

struct task {
    u64          rsp;                    /* saved stack pointer (context switch) */
    u64          stack_top;             /* top of this task's stack */
    u64          stack_size;
    int          state;
    int          tid;
    char         name[TASK_NAME_LEN];
    u64          ticks;                  /* total timer ticks consumed */
};

/* Initialise the scheduler and create the idle task */
void sched_init(void);

/* Create a new kernel task; returns tid ≥ 0 on success, -1 on failure */
int  task_create(const char *name, task_func_t fn, void *arg, size_t stack_size);

/* Called from the timer interrupt handler */
void sched_tick(void);

/* Voluntarily yield to the scheduler */
void sched_yield(void);

/* Returns a pointer to the currently running task */
struct task *sched_current(void);

/* Architecture-specific context switch (boot/x86_64/switch.S) */
void switch_to(struct task *prev, struct task *next);

#endif /* CK_SCHED_H */
