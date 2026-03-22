/*
 * include/ck/spinlock.h
 *
 * Minimal ticket-based spinlock and IRQ-safe spinlock for ComputeKERNEL.
 *
 * Ticket spinlocks provide FIFO fairness: waiters are served in the order
 * they arrived, preventing starvation on contended locks.
 *
 * Usage:
 *   spinlock_t lk = SPINLOCK_INIT;
 *   spin_lock(&lk);
 *   ... critical section ...
 *   spin_unlock(&lk);
 *
 * IRQ-safe variant (disables interrupts while held):
 *   u64 flags = spin_lock_irqsave(&lk);
 *   ... critical section ...
 *   spin_unlock_irqrestore(&lk, flags);
 */
#ifndef CK_SPINLOCK_H
#define CK_SPINLOCK_H

#include <ck/types.h>
#include <ck/io.h>

typedef struct {
    volatile u16 next;   /* next ticket to issue */
    volatile u16 owner;  /* ticket currently being served */
} spinlock_t;

#define SPINLOCK_INIT { .next = 0, .owner = 0 }

static inline void spin_lock_init(spinlock_t *lk)
{
    lk->next  = 0;
    lk->owner = 0;
}

static inline void spin_lock(spinlock_t *lk)
{
    /* Atomically fetch-and-increment lk->next to get our ticket */
    u16 ticket = __atomic_fetch_add(&lk->next, 1, __ATOMIC_SEQ_CST);
    /* Spin until it's our turn */
    while (__atomic_load_n(&lk->owner, __ATOMIC_ACQUIRE) != ticket)
        cpu_pause();
}

static inline void spin_unlock(spinlock_t *lk)
{
    /* Advance owner to the next ticket */
    __atomic_fetch_add(&lk->owner, 1, __ATOMIC_RELEASE);
}

static inline int spin_trylock(spinlock_t *lk)
{
    u16 cur_next  = __atomic_load_n(&lk->next,  __ATOMIC_RELAXED);
    u16 cur_owner = __atomic_load_n(&lk->owner, __ATOMIC_RELAXED);
    if (cur_next != cur_owner)
        return 0; /* already held */
    /* Try to claim the ticket */
    return __atomic_compare_exchange_n(&lk->next, &cur_next,
                                       (u16)(cur_next + 1),
                                       0,
                                       __ATOMIC_SEQ_CST,
                                       __ATOMIC_RELAXED);
}

/* IRQ-safe variants: disable interrupts before acquiring the lock */
static inline u64 spin_lock_irqsave(spinlock_t *lk)
{
    u64 flags = irq_save();
    spin_lock(lk);
    return flags;
}

static inline void spin_unlock_irqrestore(spinlock_t *lk, u64 flags)
{
    spin_unlock(lk);
    irq_restore(flags);
}

#endif /* CK_SPINLOCK_H */
