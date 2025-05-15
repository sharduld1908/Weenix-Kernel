#include "kernel.h"
#include "config.h"
#include "globals.h"
#include "errno.h"

#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"
#include "util/printf.h"

#include "proc/kthread.h"
#include "proc/proc.h"
#include "proc/sched.h"
#include "proc/proc.h"

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mmobj.h"
#include "mm/mm.h"
#include "mm/mman.h"

#include "vm/vmmap.h"

#include "fs/vfs.h"
#include "fs/vfs_syscall.h"
#include "fs/vnode.h"
#include "fs/file.h"

proc_t *curproc = NULL; /* global */
static slab_allocator_t *proc_allocator = NULL;

static list_t _proc_list;
static proc_t *proc_initproc = NULL; /* Pointer to the init process (PID 1) */

void
proc_init()
{
        list_init(&_proc_list);
        proc_allocator = slab_allocator_create("proc", sizeof(proc_t));
        KASSERT(proc_allocator != NULL);
}

proc_t *
proc_lookup(int pid)
{
        proc_t *p;
        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                if (p->p_pid == pid) {
                        return p;
                }
        } list_iterate_end();
        return NULL;
}

list_t *
proc_list()
{
        return &_proc_list;
}

size_t
proc_info(const void *arg, char *buf, size_t osize)
{
        const proc_t *p = (proc_t *) arg;
        size_t size = osize;
        proc_t *child;

        KASSERT(NULL != p);
        KASSERT(NULL != buf);

        iprintf(&buf, &size, "pid:          %i\n", p->p_pid);
        iprintf(&buf, &size, "name:         %s\n", p->p_comm);
        if (NULL != p->p_pproc) {
                iprintf(&buf, &size, "parent:       %i (%s)\n",
                        p->p_pproc->p_pid, p->p_pproc->p_comm);
        } else {
                iprintf(&buf, &size, "parent:       -\n");
        }

#ifdef __MTP__
        int count = 0;
        kthread_t *kthr;
        list_iterate_begin(&p->p_threads, kthr, kthread_t, kt_plink) {
                ++count;
        } list_iterate_end();
        iprintf(&buf, &size, "thread count: %i\n", count);
#endif

        if (list_empty(&p->p_children)) {
                iprintf(&buf, &size, "children:     -\n");
        } else {
                iprintf(&buf, &size, "children:\n");
        }
        list_iterate_begin(&p->p_children, child, proc_t, p_child_link) {
                iprintf(&buf, &size, "     %i (%s)\n", child->p_pid, child->p_comm);
        } list_iterate_end();

        iprintf(&buf, &size, "status:       %i\n", p->p_status);
        iprintf(&buf, &size, "state:        %i\n", p->p_state);

#ifdef __VFS__
#ifdef __GETCWD__
        if (NULL != p->p_cwd) {
                char cwd[256];
                lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                iprintf(&buf, &size, "cwd:          %-s\n", cwd);
        } else {
                iprintf(&buf, &size, "cwd:          -\n");
        }
#endif /* __GETCWD__ */
#endif

#ifdef __VM__
        iprintf(&buf, &size, "start brk:    0x%p\n", p->p_start_brk);
        iprintf(&buf, &size, "brk:          0x%p\n", p->p_brk);
#endif

        return size;
}

size_t
proc_list_info(const void *arg, char *buf, size_t osize)
{
        size_t size = osize;
        proc_t *p;

        KASSERT(NULL == arg);
        KASSERT(NULL != buf);

#if defined(__VFS__) && defined(__GETCWD__)
        iprintf(&buf, &size, "%5s %-13s %-18s %-s\n", "PID", "NAME", "PARENT", "CWD");
#else
        iprintf(&buf, &size, "%5s %-13s %-s\n", "PID", "NAME", "PARENT");
#endif

        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                char parent[64];
                if (NULL != p->p_pproc) {
                        snprintf(parent, sizeof(parent),
                                 "%3i (%s)", p->p_pproc->p_pid, p->p_pproc->p_comm);
                } else {
                        snprintf(parent, sizeof(parent), "  -");
                }

#if defined(__VFS__) && defined(__GETCWD__)
                if (NULL != p->p_cwd) {
                        char cwd[256];
                        lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                        iprintf(&buf, &size, " %3i  %-13s %-18s %-s\n",
                                p->p_pid, p->p_comm, parent, cwd);
                } else {
                        iprintf(&buf, &size, " %3i  %-13s %-18s -\n",
                                p->p_pid, p->p_comm, parent);
                }
#else
                iprintf(&buf, &size, " %3i  %-13s %-s\n",
                        p->p_pid, p->p_comm, parent);
#endif
        } list_iterate_end();
        return size;
}

static pid_t next_pid = 0;

/**
 * Returns the next available PID.
 *
 * Note: Where n is the number of running processes, this algorithm is
 * worst case O(n^2). As long as PIDs never wrap around it is O(n).
 *
 * @return the next available PID
 */
static int
_proc_getid()
{
        proc_t *p;
        pid_t pid = next_pid;
        while (1) {
            failed:
                list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                        if (p->p_pid == pid) {
                                if ((pid = (pid + 1) % PROC_MAX_COUNT) == next_pid) {
                                        return -1;
                                } else {
                                        goto failed;
                                }
                        }
                } list_iterate_end();
                next_pid = (pid + 1) % PROC_MAX_COUNT;
                return pid;
        }
}

/*
 * The new process, although it isn't really running since it has no
 * threads, should be in the PROC_RUNNING state.
 *
 * Don't forget to set proc_initproc when you create the init
 * process. You will need to be able to reference the init process
 * when reparenting processes to the init process.
 */
proc_t *
proc_create(char *name)
{
        proc_t *p;
        p = slab_obj_alloc(proc_allocator);
        KASSERT(p != NULL);

        memset(p, 0, sizeof(proc_t));
        
        pid_t pid = _proc_getid();
        
        KASSERT(PID_IDLE != pid || list_empty(&_proc_list));
        KASSERT(PID_INIT != pid || PID_IDLE == curproc->p_pid);
        
        p->p_pid = pid;
        if(pid == 1) {
            proc_initproc = p;
        }

        // Naming the process
        strcpy(p->p_comm, name);
        p->p_status = 0;

        list_init(&(p->p_threads));
        list_init(&(p->p_children));

        if (curproc != NULL)
        {
            list_insert_head(&curproc->p_children, &p->p_child_link);
        }

        p->p_pproc = curproc;
        p->p_state = PROC_RUNNING;
        
        sched_queue_init(&p->p_wait);
        
        p->p_pagedir = pt_create_pagedir();
        list_insert_head(&_proc_list, &p->p_list_link);

// #ifdef __VFS__

        for (int i = 0; i < NFILES; i++)
        {
                p->p_files[i] = NULL;
        }

        if (p->p_pid > 1)
        {
            if (vfs_root_vn)
            {
                p->p_cwd = vfs_root_vn;
                vget(vfs_root_vn->vn_fs, vfs_root_vn->vn_vno);
            }
        }

// # endif  

#ifdef __VM__

        p->p_vmmap = vmmap_create();
        p->p_vmmap->vmm_proc = p;
        if (pid != PID_IDLE)
        {
                p->p_brk = curproc->p_brk;
                p->p_start_brk = curproc->p_start_brk;
        }

#endif

        return p;
}

/**
 * Cleans up as much as the process as can be done from within the
 * process. This involves:
 *    - Closing all open files (VFS)
 *    - Cleaning up VM mappings (VM)
 *    - Waking up its parent if it is waiting
 *    - Reparenting any children to the init process
 *    - Setting its status and state appropriately
 *
 * The parent will finish destroying the process within do_waitpid (make
 * sure you understand why it cannot be done here). Until the parent
 * finishes destroying it, the process is informally called a 'zombie'
 * process.
 *
 * This is also where any children of the current process should be
 * reparented to the init process (unless, of course, the current
 * process is the init process. However, the init process should not
 * have any children at the time it exits).
 *
 * Note: You do _NOT_ have to special case the idle process. It should
 * never exit this way.
 *
 * @param status the status to exit the process with
 */
void
proc_cleanup(int status)
{       
        // TODO: Check logic for this function.
    
        KASSERT(NULL != proc_initproc);
        KASSERT(1 <= curproc->p_pid);
        KASSERT(NULL != curproc->p_pproc);

        proc_t* parent_process = curthr->kt_proc->p_pproc;
        if(!sched_queue_empty(&parent_process->p_wait))
        {
            sched_wakeup_on(&parent_process->p_wait);
        }

        proc_t* child;
        list_iterate_begin(&curproc->p_children, child, proc_t, p_child_link) {
                list_insert_head(&proc_initproc->p_children, &child->p_child_link);
                child->p_pproc = proc_initproc;
        } list_iterate_end();

        curproc->p_status = status;
        curproc->p_state = PROC_DEAD;

        for (int i = 0; i < NFILES; i++)
        {
                if (curproc->p_files[i])
                {

                        fput(curproc->p_files[i]);
                }
                curproc->p_files[i] = NULL;
        }

        if (curproc->p_cwd)
        {
                vput(curproc->p_cwd);
        }

        vmmap_destroy(curproc->p_vmmap);

        KASSERT(NULL != curproc->p_pproc);
        KASSERT(KT_EXITED == curthr->kt_state);
}

/*
 * This has nothing to do with signals and kill(1).
 *
 * Calling this on the current process is equivalent to calling
 * do_exit().
 *
 * In Weenix, this is only called from proc_kill_all.
 */
void
proc_kill(proc_t *p, int status)
{
        if(curproc == p) {
                do_exit(status);
        }
        list_t thread_list = p->p_threads;
        kthread_t* proc_thread = list_head(&thread_list, kthread_t, kt_plink);
        kthread_cancel(proc_thread, NULL);
}

/*
 * Remember, proc_kill on the current process will _NOT_ return.
 * Don't kill direct children of the idle process.
 *
 * In Weenix, this is only called by sys_halt.
 */
void
proc_kill_all()
{
        proc_t* proc;
        list_iterate_begin(&_proc_list, proc, proc_t, p_list_link) {
                if(proc->p_pid != 0 && proc->p_pid != 1 && proc->p_pid != 2) {
                        if(curproc != proc) {
                                proc_kill(proc, 0);
                        }
                }
        } list_iterate_end();
}

/*
 * This function is only called from kthread_exit.
 *
 * Unless you are implementing MTP, this just means that the process
 * needs to be cleaned up and a new thread needs to be scheduled to
 * run. If you are implementing MTP, a single thread exiting does not
 * necessarily mean that the process should be exited.
 */
void
proc_thread_exited(void *retval)
{
        proc_cleanup((int)retval);
        sched_switch();
}

/* If pid is -1 dispose of one of the exited children of the current
 * process and return its exit status in the status argument, or if
 * all children of this process are still running, then this function
 * blocks on its own p_wait queue until one exits.
 *
 * If pid is greater than 0 and the given pid is a child of the
 * current process then wait for the given pid to exit and dispose
 * of it.
 *
 * If the current process has no children, or the given pid is not
 * a child of the current process return -ECHILD.
 *
 * Pids other than -1 and positive numbers are not supported.
 * Options other than 0 are not supported.
 */
pid_t
do_waitpid(pid_t pid, int options, int *status)
{
    pid_t return_val = 0;
    proc_t *child;
    if (options != 0) {
        return -EINVAL;
    }

    if (pid == -1) {
        
        int is_dead_child = 0;

        //check if current process has any children
        if (list_empty(&curproc->p_children)) {
            return -ECHILD;
        }
        while (!is_dead_child) {
                list_iterate_begin(&curproc->p_children, child, proc_t, p_child_link) {
                        if (child->p_state == PROC_DEAD) {

                                KASSERT(NULL != child);
                                KASSERT(-1 == pid || child->p_pid == pid); 
                                KASSERT(NULL != child->p_pagedir);
                                
                                is_dead_child = 1;

                                //set status as the exit status of the process
                                if(status != NULL)
                                    *status = child->p_status;
    
                                return_val = child->p_pid;

                                //dispose of the process
                                kthread_t *kthread_process_dead;

                                /* obtain least-recently-requested page: */
                                // only 1 thread as MTP == 1
                                kthread_process_dead = list_head(&child->p_threads, kthread_t, kt_plink);

                                kthread_destroy(kthread_process_dead);
                                // remove the dead child from parent's process

                                list_remove(&child->p_list_link);
                                list_remove(&child->p_child_link);

                                pt_destroy_pagedir(child->p_pagedir);
                                slab_obj_free(proc_allocator, child);

                                //return the disposed process' pid
                                return return_val;
                        }
                    } list_iterate_end();
            if (!is_dead_child) {
                sched_sleep_on(&curproc->p_wait);
            }
        }
    } 
    else if (pid > 0) {
        int is_child_of_process = 0;
        
        //check if current process has any children
        if (list_empty(&curproc->p_children)) {
            return -ECHILD;
        }

        list_iterate_begin(&curproc->p_children, child, proc_t, p_child_link) {
                    if (child->p_pid == pid) {
                        is_child_of_process = 1;
                        
                        while(1){

                            if (child->p_state == PROC_DEAD) {

                                KASSERT(NULL != child);
                                KASSERT(-1 == pid || child->p_pid == pid); 
                                KASSERT(NULL != child->p_pagedir);

                                //set status as the exit status of the process
                                if(status != NULL)
                                    *status = child->p_status;
                                
                                return_val = child->p_pid;

                                //dispose of the process
                                kthread_t *kthread_process_dead;

                                /* obtain least-recently-requested page: */
                                // only 1 thread as MTP == 1
                                kthread_process_dead = list_head(&child->p_threads, kthread_t, kt_plink);

                                // remove the dead child from parent's process
                                list_remove(&child->p_child_link);
                                list_remove(&child->p_list_link);

                                kthread_destroy(kthread_process_dead);

                                pt_destroy_pagedir(child->p_pagedir);
                                slab_obj_free(proc_allocator, child);

                                //return the disposed process' pid
                                return return_val;
                            } else {
                                sched_sleep_on(&curproc->p_wait);
                            }
                        }
                    }
                } list_iterate_end();

        if (!is_child_of_process) {
            return -ECHILD;
        }

    } else {
        return -EINVAL;
    }

    return 0;
}

/*
 * Cancel all threads and join with them (if supporting MTP), and exit from the current
 * thread.
 *
 * @param status the exit status of the process
 */
void
do_exit(int status)
{
    kthread_exit((void*)status);
}
