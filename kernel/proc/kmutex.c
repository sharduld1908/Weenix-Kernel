#include "globals.h"
#include "errno.h"

#include "util/debug.h"

#include "proc/kthread.h"
#include "proc/kmutex.h"

/*
 * IMPORTANT: Mutexes can _NEVER_ be locked or unlocked from an
 * interrupt context. Mutexes are _ONLY_ lock or unlocked from a
 * thread context.
 */

void
kmutex_init(kmutex_t *mtx)
{
        mtx->km_holder = NULL;
        sched_queue_init(&mtx->km_waitq);
}

/*
 * This should block the current thread (by sleeping on the mutex's
 * wait queue) if the mutex is already taken.
 *
 * No thread should ever try to lock a mutex it already has locked.
 */
void
kmutex_lock(kmutex_t *mtx)
{
        KASSERT(curthr && (curthr != mtx->km_holder));
        if(mtx->km_holder != NULL) {
                sched_sleep_on(&mtx->km_waitq);
        }
        else {
                mtx->km_holder = curthr;
        }
}

/*
 * This should do the same as kmutex_lock, but use a cancellable sleep
 * instead. Also, if you are cancelled while holding mtx, you should unlock mtx.
 */
int
kmutex_lock_cancellable(kmutex_t *mtx)
{
        KASSERT(curthr && (curthr != mtx->km_holder));

        if(curthr->kt_cancelled != 1) {
        
                if(mtx->km_holder != NULL) {
                        int retval = sched_cancellable_sleep_on(&mtx->km_waitq);
                        if(retval == -EINTR) {
                                kmutex_unlock(mtx);
                        }
                        return retval;
                }
                else {
                        mtx->km_holder = curthr;
                        return 0;
                }
        }

        return -EINTR;
}

/*
 * If there are any threads waiting to take a lock on the mutex, one
 * should be woken up and given the lock.
 *
 * Note: This should _NOT_ be a blocking operation!
 *
 * Note: Ensure the new owner of the mutex enters the run queue.
 *
 * Note: Make sure that the thread on the head of the mutex's wait
 * queue becomes the new owner of the mutex.
 *
 * @param mtx the mutex to unlock
 */
void
kmutex_unlock(kmutex_t *mtx)
{
        KASSERT(curthr && (curthr == mtx->km_holder));

        if(sched_queue_empty(&mtx->km_waitq)) {
                mtx->km_holder = NULL;
        }
        else {
                mtx->km_holder = sched_wakeup_on(&mtx->km_waitq);
        }

        KASSERT(curthr != mtx->km_holder);
}
