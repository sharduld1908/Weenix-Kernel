#include "types.h"
#include "globals.h"
#include "errno.h"

#include "util/debug.h"
#include "util/string.h"

#include "proc/proc.h"
#include "proc/kthread.h"

#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/page.h"
#include "mm/pframe.h"
#include "mm/mmobj.h"
#include "mm/pagetable.h"
#include "mm/tlb.h"

#include "fs/file.h"
#include "fs/vnode.h"

#include "vm/shadow.h"
#include "vm/vmmap.h"

#include "api/exec.h"

#include "main/interrupt.h"

/* Pushes the appropriate things onto the kernel stack of a newly forked thread
 * so that it can begin execution in userland_entry.
 * regs: registers the new thread should have on execution
 * kstack: location of the new thread's kernel stack
 * Returns the new stack pointer on success. */
static uint32_t
fork_setup_stack(const regs_t *regs, void *kstack)
{
        /* Pointer argument and dummy return address, and userland dummy return
         * address */
        uint32_t esp = ((uint32_t) kstack) + DEFAULT_STACK_SIZE - (sizeof(regs_t) + 12);
        *(void **)(esp + 4) = (void *)(esp + 8); /* Set the argument to point to location of struct on stack */
        memcpy((void *)(esp + 8), regs, sizeof(regs_t)); /* Copy over struct */
        return esp;
}


/*
 * The implementation of fork(2). Once this works,
 * you're practically home free. This is what the
 * entirety of Weenix has been leading up to.
 * Go forth and conquer.
 */
int
do_fork(struct regs *regs)
{

        // fork KASSERTs grading guideline
        KASSERT(regs != NULL); /* the function argument must be non-NULL */
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");
        KASSERT(curproc != NULL); /* the parent process, which is curproc, must be non-NULL */
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");
        KASSERT(curproc->p_state == PROC_RUNNING); /* the parent process must be in the running state and not in the zombie state */
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");

        // fork approcah -
                // create child
                // here is where we will need to call the vmmap_clone function (for cloning the vmmap for child process from the parent)
                        // iterating through the vma list and update populate the child area?

        proc_t *newproc = NULL;

        // the new process - child
        newproc = proc_create("newproc");
        newproc->p_vmmap = vmmap_clone(curproc->p_vmmap);

        KASSERT(newproc->p_state == PROC_RUNNING); /* new child process starts in the running state */
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");
        KASSERT(newproc->p_pagedir != NULL); /* new child process must have a valid page table */
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");

        // vma list iterate

        list_link_t *list_link_obj = curproc->p_vmmap->vmm_list.l_next;

        vmarea_t *vma_p, *vma_c;
        list_iterate_begin(&newproc->p_vmmap->vmm_list, vma_c, vmarea_t, vma_plink)
        {
                vma_p = list_item(list_link_obj, vmarea_t, vma_plink);
                
                // mmobj
                vma_c->vma_obj = vma_p->vma_obj;
                vma_p->vma_obj->mmo_ops->ref(vma_p->vma_obj);
                
                // priv shad
                if ((vma_p->vma_flags & MAP_TYPE) == MAP_PRIVATE)
                {
                        // child
                        mmobj_t *mmobj_shad_c = shadow_create();
                        
                        mmobj_t *bottom_obj = mmobj_bottom_obj(vma_p->vma_obj);
                        mmobj_shad_c->mmo_un.mmo_bottom_obj = bottom_obj;
                        mmobj_shad_c->mmo_shadowed = vma_c->vma_obj;
                        list_insert_tail(&bottom_obj->mmo_un.mmo_vmas, &vma_c->vma_olink);
                        
                        vma_c->vma_obj = mmobj_shad_c;

                        // parent
                        mmobj_t *mmobj_shad_p = shadow_create();
                        mmobj_shad_p->mmo_un.mmo_bottom_obj = bottom_obj;
                        mmobj_shad_p->mmo_shadowed = vma_p->vma_obj;
                        if (list_link_is_linked(&vma_p->vma_olink))
                        {
                                list_remove(&vma_p->vma_olink);
                                dbg(DBG_PRINT, "(GRADING3A)\n");
                        }
                        list_insert_tail(&bottom_obj->mmo_un.mmo_vmas, &vma_p->vma_olink);
                        vma_p->vma_obj = mmobj_shad_p;
                        dbg(DBG_PRINT, "(GRADING3A)\n");
                }
                list_link_obj = list_link_obj->l_next;
                dbg(DBG_PRINT, "(GRADING3A)\n");
        }
        list_iterate_end();

        kthread_t *newthr = kthread_clone(curthr);
        KASSERT(newthr->kt_kstack != NULL); /* thread in the new child process must have a valid kernel stack */
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");
        newproc->p_brk = curproc->p_brk;
        newproc->p_start_brk = curproc->p_start_brk;

        // unmap the whole range
        pt_unmap_range(curproc->p_pagedir, USER_MEM_LOW, USER_MEM_HIGH);

        // TLB
        tlb_flush_all();

        for (int i = 0; i < NFILES; i++)
        {
                newproc->p_files[i] = curproc->p_files[i];
                if (newproc->p_files[i] != NULL)
                {
                        fref(newproc->p_files[i]);
                        dbg(DBG_PRINT, "(GRADING3A)\n");
                }
                dbg(DBG_PRINT, "(GRADING3A)\n");
        }

        vput(newproc->p_cwd);

        regs->r_eax = 0;

        // new thread reg and proc
        newthr->kt_proc = newproc;
        list_insert_tail(&newproc->p_threads, &newthr->kt_plink);
        
        newthr->kt_ctx.c_eip = (uint32_t)userland_entry;
        newthr->kt_ctx.c_esp = fork_setup_stack(regs, newthr->kt_kstack);
        newthr->kt_ctx.c_kstack = (uintptr_t)newthr->kt_kstack;
        
        newthr->kt_ctx.c_kstacksz = DEFAULT_STACK_SIZE;
        newthr->kt_ctx.c_pdptr = newproc->p_pagedir;

        sched_make_runnable(newthr);

        newproc->p_cwd = curproc->p_cwd;
        vref(newproc->p_cwd);

        dbg(DBG_PRINT, "(GRADING3A)\n");
        return newproc->p_pid;
}
