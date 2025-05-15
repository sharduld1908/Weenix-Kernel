#include "types.h"
#include "globals.h"
#include "kernel.h"
#include "errno.h"

#include "util/gdb.h"
#include "util/init.h"
#include "util/debug.h"
#include "util/string.h"
#include "util/printf.h"

#include "mm/mm.h"
#include "mm/page.h"
#include "mm/pagetable.h"
#include "mm/pframe.h"

#include "vm/vmmap.h"
#include "vm/shadowd.h"
#include "vm/shadow.h"
#include "vm/anon.h"

#include "main/acpi.h"
#include "main/apic.h"
#include "main/interrupt.h"
#include "main/gdt.h"

#include "proc/sched.h"
#include "proc/proc.h"
#include "proc/kthread.h"

#include "drivers/dev.h"
#include "drivers/blockdev.h"
#include "drivers/disk/ata.h"
#include "drivers/tty/virtterm.h"
#include "drivers/pci.h"

#include "api/exec.h"
#include "api/syscall.h"

#include "fs/vfs.h"
#include "fs/vnode.h"
#include "fs/vfs_syscall.h"
#include "fs/fcntl.h"
#include "fs/stat.h"

#include "test/kshell/kshell.h"
#include "test/s5fs_test.h"

GDB_DEFINE_HOOK(initialized)

void      *bootstrap(int arg1, void *arg2);
void      *idleproc_run(int arg1, void *arg2);
kthread_t *initproc_create(void);
void      *initproc_run(int arg1, void *arg2);
void      *final_shutdown(void);

/**
 * This function is called from kmain, however it is not running in a
 * thread context yet. It should create the idle process which will
 * start executing idleproc_run() in a real thread context.  To start
 * executing in the new process's context call context_make_active(),
 * passing in the appropriate context. This function should _NOT_
 * return.
 *
 * Note: Don't forget to set curproc and curthr appropriately.
 *
 * @param arg1 the first argument (unused)
 * @param arg2 the second argument (unused)
 */
void *
bootstrap(int arg1, void *arg2)
{
        /* If the next line is removed/altered in your submission, 20 points will be deducted. */
        dbgq(DBG_TEST, "SIGNATURE: 53616c7465645f5fd87b7b169bb70943008d3224df8344be8fa1144ce8558efc5d28cbe0a7b380c3f7b8ad0735c949ed\n");
        /* necessary to finalize page table information */
        pt_template_init();
        
        // Create IDLE Process
        curproc = proc_create("idle_proc");

        KASSERT(NULL != curproc);
        KASSERT(PID_IDLE == curproc->p_pid);

        // Create IDLE Thread
        curthr = kthread_create(curproc, idleproc_run, 0, NULL);
        
        KASSERT(NULL != curthr);
        
        // Start running in the idle process's context
        context_make_active(&curthr->kt_ctx);

        panic("weenix returned to bootstrap()!!! BAD!!!\n");
        return NULL;
}

/**
 * Once we're inside of idleproc_run(), we are executing in the context of the
 * first process-- a real context, so we can finally begin running
 * meaningful code.
 *
 * This is the body of process 0. It should initialize all that we didn't
 * already initialize in kmain(), launch the init process (initproc_run),
 * wait for the init process to exit, then halt the machine.
 *
 * @param arg1 the first argument (unused)
 * @param arg2 the second argument (unused)
 */
void *
idleproc_run(int arg1, void *arg2)
{
        int status;
        pid_t child;

        /* create init proc */
        kthread_t *initthr = initproc_create();
        init_call_all();
        GDB_CALL_HOOK(initialized);

        /* Create other kernel threads (in order) */
#ifdef __VFS__
        /* Once you have VFS remember to set the current working directory
         * of the idle and init processes */
        //NOT_YET_IMPLEMENTED("VFS: idleproc_run");
        curproc->p_cwd = vfs_root_vn;
        vref(vfs_root_vn);
        
        initthr->kt_proc->p_cwd = vfs_root_vn;
        vref(vfs_root_vn);


        /* Here you need to make the null, zero, and tty devices using mknod */
        /* You can't do this until you have VFS, check the include/drivers/dev.h
         * file for macros with the device ID's you will need to pass to mknod */
        //create dev directory
        do_mkdir("/dev");
        
        //create null device
        do_mknod("/dev/null",S_IFCHR, MEM_NULL_DEVID);
        
        //create zero device
        do_mknod("/dev/zero",S_IFCHR, MEM_ZERO_DEVID);

        //create tty device
        do_mknod("/dev/tty0",S_IFCHR, MKDEVID(2,0));


#endif

        /* Finally, enable interrupts (we want to make sure interrupts
         * are enabled AFTER all drivers are initialized) */
        intr_enable();

        /* Run initproc */
        sched_make_runnable(initthr);
        /* Now wait for it */
        child = do_waitpid(-1, 0, &status);
        KASSERT(PID_INIT == child);
        
        //struct stat buf;
        //do_stat("/",&buf);
        //dbg(DBG_PRINT,"stat nlink : %d",buf.st_nlink);
        return final_shutdown();
}

/**
 * This function, called by the idle process (within 'idleproc_run'), creates the
 * process commonly refered to as the "init" process, which should have PID 1.
 *
 * The init process should contain a thread which begins execution in
 * initproc_run().
 *
 * @return a pointer to a newly created thread which will execute
 * initproc_run when it begins executing
 */
kthread_t *
initproc_create(void)
{
        // Create INIT Process
        proc_t *p;
        p = proc_create("init");

        KASSERT(NULL != p);
        KASSERT(PID_INIT == p->p_pid);

        // Create INIT Thread
        kthread_t *k;
        k = kthread_create(p, initproc_run, 0, NULL);
        
        KASSERT(NULL != k);
        
        return k;
}

void * faber_thread_test(int argc, void* argv);
void * sunghan_test(int argc, void* argv);
void * sunghan_deadlock_test(int argc, void* argv);
int vfstest_main(int argc, char **argv);
int faber_directory_test(kshell_t *kshell, int argc, char **argv);
int faber_fs_thread_test(kshell_t *ksh, int argc, char **argv);

#ifdef __DRIVERS__

    int do_faber_test(kshell_t *kshell, int argc, char **argv){
        KASSERT(kshell != NULL);
        //Create the faber process
        proc_t *pt_faber;
        pt_faber = proc_create("FABER");
        
        kthread_t *kt_faber;
        kt_faber = kthread_create(pt_faber, faber_thread_test, 0, NULL);
        
        int status;
        sched_make_runnable(kt_faber);
        do_waitpid(pt_faber->p_pid, 0, &status);

        return 0;
    }

    int do_sunghan_test(kshell_t *kshell, int argc, char **argv){
        KASSERT(kshell != NULL);
        //Create the faber process
        proc_t *pt_sunghan;
        pt_sunghan = proc_create("SUNGHAN");
        
        kthread_t *kt_sunghan;
        kt_sunghan = kthread_create(pt_sunghan, sunghan_test, 0, NULL);
 
        int status;        
        sched_make_runnable(kt_sunghan);
        do_waitpid(pt_sunghan->p_pid, 0, &status);

        return 0;
    }

    int do_deadlock_test(kshell_t *kshell, int argc, char **argv){
        KASSERT(kshell != NULL);
        //Create the sunghan deadlock process
        proc_t *pt_deadlock;
        pt_deadlock = proc_create("SUNGHAN");
        
        kthread_t *kt_deadlock;
        kt_deadlock = kthread_create(pt_deadlock, sunghan_deadlock_test, 0, NULL);
        
        int status;
        sched_make_runnable(kt_deadlock);
        do_waitpid(pt_deadlock->p_pid, 0, &status);
        
        return 0;
    }

    int do_vfs_test(kshell_t *kshell, int argc, char **argv) {
        
        KASSERT(kshell != NULL);
        //Create the faber process
        proc_t *pt_vfstest;
        pt_vfstest = proc_create("VFSTEST");
        pt_vfstest->p_cwd = curproc->p_cwd;
        vref(pt_vfstest->p_cwd);

        kthread_t *kt_vfstest;
        kt_vfstest = kthread_create(pt_vfstest, (void*)vfstest_main, 1, NULL);
        
        int status;
        sched_make_runnable(kt_vfstest);
        do_waitpid(pt_vfstest->p_pid, 0, &status);

        vput(pt_vfstest->p_cwd);
        
        return 0;
    }

#endif /* __DRIVERS__ */

/**
 * The init thread's function changes depending on how far along your Weenix is
 * developed. Before VM/FI, you'll probably just want to have this run whatever
 * tests you've written (possibly in a new process). After VM/FI, you'll just
 * exec "/sbin/init".
 *
 * Both arguments are unused.
 *
 * @param arg1 the first argument (unused)
 * @param arg2 the second argument (unused)
 */
void *
initproc_run(int arg1, void *arg2)
{
    char *const argvec[] = {"/sbin/init", NULL};
    // char *const argvec[] = { NULL};
    char *const envvec[] = {NULL};
    kernel_execve("/sbin/init", argvec, envvec);
    // kernel_execve("/usr/bin/hello", argvec, envvec);

    return NULL;
    // #ifdef __DRIVERS__

    //     kshell_add_command("faber", do_faber_test, "Run faber_thread_test().");
    //     kshell_add_command("sunghan", do_sunghan_test, "Run sunghan_test().");
    //     kshell_add_command("deadlock", do_deadlock_test, "Run sunghan_deadlock_test().");

    // #ifdef __VFS__

    //     kshell_add_command("vfstest", do_vfs_test, "Run vfstest()");
    //     kshell_add_command("directory_test", faber_directory_test, "Run faber_directory_test()");
    //     kshell_add_command("thread_test", faber_fs_thread_test, "Run faber_fs_thread_test()");
    
    // #endif

    //     kshell_t *kshell = kshell_create(0);
    //     if (NULL == kshell) panic("init: Couldn't create kernel shell\n");
        
    //     while (kshell_execute_next(kshell));
    //     kshell_destroy(kshell);

    // #endif /* __DRIVERS__ */

    // while(do_waitpid(-1, 0, NULL) != -ECHILD);
    // return NULL;
}
