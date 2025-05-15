#include "globals.h"
#include "errno.h"

#include "main/interrupt.h"

#include "proc/sched.h"
#include "proc/kthread.h"

#include "util/init.h"
#include "util/debug.h"

void ktqueue_enqueue(ktqueue_t *q, kthread_t *thr);
kthread_t * ktqueue_dequeue(ktqueue_t *q);

/*
 * Updates the thread's state and enqueues it on the given
 * queue. Returns when the thread has been woken up with wakeup_on or
 * broadcast_on.
 *
 * Use the private queue manipulation functions above.
 */
void
sched_sleep_on(ktqueue_t *q)
{
        curthr->kt_state = KT_SLEEP;
        ktqueue_enqueue(q, curthr);
        sched_switch();
}

kthread_t *
sched_wakeup_on(ktqueue_t *q)
{
        kthread_t* thread_on_queue = ktqueue_dequeue(q);
        if(thread_on_queue == NULL)
        {
                return NULL;
        }
        
        KASSERT((thread_on_queue->kt_state == KT_SLEEP) || (thread_on_queue->kt_state == KT_SLEEP_CANCELLABLE));
        sched_make_runnable(thread_on_queue);
        return thread_on_queue;
}

void
sched_broadcast_on(ktqueue_t *q)
{
        while (sched_wakeup_on(q) != NULL);
}

