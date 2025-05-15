/******************************************************************************/
/* Important Spring 2024 CSCI 402 usage information:                          */
/*                                                                            */
/* This fils is part of CSCI 402 kernel programming assignments at USC.       */
/*         53616c7465645f5fd1e93dbf35cbffa3aef28f8c01d8cf2ffc51ef62b26a       */
/*         f9bda5a68e5ed8c972b17bab0f42e24b19daa7bd408305b1f7bd6c7208c1       */
/*         0e36230e913039b3046dd5fd0ba706a624d33dbaa4d6aab02c82fe09f561       */
/*         01b0fd977b0051f0b0ce0c69f7db857b1b5e007be2db6d42894bf93de848       */
/*         806d9152bd5715e9                                                   */
/* Please understand that you are NOT permitted to distribute or publically   */
/*         display a copy of this file (or ANY PART of it) for any reason.    */
/* If anyone (including your prospective employer) asks you to post the code, */
/*         you must inform them that you do NOT have permissions to do so.    */
/* You are also NOT permitted to remove or alter this comment block.          */
/* If this comment block is removed or altered in a submitted file, 20 points */
/*         will be deducted.                                                  */
/******************************************************************************/

#include "globals.h"
#include "errno.h"
#include "util/debug.h"

#include "mm/mm.h"
#include "mm/page.h"
#include "mm/mman.h"

#include "vm/mmap.h"
#include "vm/vmmap.h"

#include "proc/proc.h"

/*
 * This function implements the brk(2) system call.
 *
 * This routine manages the calling process's "break" -- the ending address
 * of the process's "dynamic" region (often also referred to as the "heap").
 * The current value of a process's break is maintained in the 'p_brk' member
 * of the proc_t structure that represents the process in question.
 *
 * The 'p_brk' and 'p_start_brk' members of a proc_t struct are initialized
 * by the loader. 'p_start_brk' is subsequently never modified; it always
 * holds the initial value of the break. Note that the starting break is
 * not necessarily page aligned!
 *
 * 'p_start_brk' is the lower limit of 'p_brk' (that is, setting the break
 * to any value less than 'p_start_brk' should be disallowed).
 *
 * The upper limit of 'p_brk' is defined by the minimum of (1) the
 * starting address of the next occuring mapping or (2) USER_MEM_HIGH.
 * That is, growth of the process break is limited only in that it cannot
 * overlap with/expand into an existing mapping or beyond the region of
 * the address space allocated for use by userland. (note the presence of
 * the 'vmmap_is_range_empty' function).
 *
 * The dynamic region should always be represented by at most ONE vmarea.
 * Note that vmareas only have page granularity, you will need to take this
 * into account when deciding how to set the mappings if p_brk or p_start_brk
 * is not page aligned.
 *
 * You are guaranteed that the process data/bss region is non-empty.
 * That is, if the starting brk is not page-aligned, its page has
 * read/write permissions.
 *
 * If addr is NULL, you should "return" the current break. We use this to
 * implement sbrk(0) without writing a separate syscall. Look in
 * user/libc/syscall.c if you're curious.
 *
 * You should support combined use of brk and mmap in the same process.
 *
 * Note that this function "returns" the new break through the "ret" argument.
 * Return 0 on success, -errno on failure.
 */
int
do_brk(void *addr, void **ret)
{
        // NOT_YET_IMPLEMENTED("VM: do_brk");
        if (addr == NULL)
        {
                *ret = curproc->p_brk;
                dbg(DBG_PRINT, "(GRADING3A)\n");
                return 0;
        }
        else
        {
                if (addr < curproc->p_start_brk)
                {
                        dbg(DBG_PRINT, "(GRADING3D 1)\n");
                        return -ENOMEM;
                }
                else if (addr == curproc->p_start_brk)
                {
                        *ret = curproc->p_start_brk;
                        dbg(DBG_PRINT, "(GRADING3D 2)\n");
                        return 0;
                }
                
        }
        
        struct vmmap *vmmap_cur = NULL;
        vmmap_cur = curproc->p_vmmap;
        vmarea_t *vma_cur = vmmap_lookup(vmmap_cur, ((uint32_t)curproc->p_start_brk - 1) >> 12);
        
        uint32_t addr_str = MAX((uint32_t)curproc->p_brk, (uint32_t)(vma_cur->vma_end) << 12);

        if ((uint32_t)addr == addr_str)
        {
                curproc->p_brk = addr;
                *ret = addr;
                dbg(DBG_PRINT, "(GRADING3D 2)\n");
                return 0;
        }

        uint32_t pgn_end = ((uint32_t)addr) >> 12;
        
        
        uint32_t pgn_start = (addr_str) >> 12;
        if (!PAGE_ALIGNED(addr))
        {
                pgn_end++;
                dbg(DBG_PRINT, "(GRADING3D 1)\n");
        }

        uint32_t npages = pgn_end - pgn_start;
        if (!PAGE_ALIGNED(addr_str))
        {
                npages--;
                 dbg(DBG_PRINT, "(GRADING3D 1)\n");
        }

        uint32_t pagenum = ((uint32_t)(curproc->p_brk) - 1) >> 12;

        if (!PAGE_ALIGNED(curproc->p_brk))
        {
                pagenum++;
                dbg(DBG_PRINT, "(GRADING3A)\n");
        }


        vmarea_t *vma = NULL;
        if (addr_str == (uint32_t)(vma_cur->vma_end) << 12)
        {
                if (addr < (void*)((vma_cur->vma_end) << 12))
                {
                        curproc->p_brk = addr;
                         dbg(DBG_PRINT, "(GRADING3D 2)\n");
                }

                else if (vmmap_is_range_empty(vmmap_cur, pgn_start, npages))
                {
                        int vmmap_ret = vmmap_map(vmmap_cur, NULL, pgn_start, npages, PROT_READ | PROT_WRITE, MAP_PRIVATE, ((uint32_t)(vma_cur->vma_end) << 12) % PAGE_SIZE, VMMAP_DIR_HILO, &vma);
                        vma->vma_end = pgn_end;

                        curproc->p_brk = addr;
                        dbg(DBG_PRINT, "(GRADING3A)\n");
                }

                else
                {
                         dbg(DBG_PRINT, "(GRADING3D 2)\n");
                        return -ENOMEM;
                }
                *ret = addr;
                dbg(DBG_PRINT, "(GRADING3A)\n");
                return 0;
        }

        if ((uint32_t)addr > (uint32_t)(vma_cur->vma_end) << 12)
        {
                if (npages == 0 || addr <= curproc->p_brk)
                {
                        vma = vmmap_lookup(vmmap_cur, ((uint32_t)curproc->p_brk - 1) >> 12);

                        dbg(DBG_PRINT, "(GRADING3D 1)\n");
                }

                else if ((vmmap_is_range_empty(vmmap_cur, pgn_start, npages)))
                {
                        vma = vmmap_lookup(vmmap_cur, pagenum);
                        dbg(DBG_PRINT, "(GRADING3A)\n");
                }
                else
                {
                         dbg(DBG_PRINT, "(GRADING3D 2)\n");
                        return -ENOMEM;
                }
                curproc->p_brk = addr;
                
                vma->vma_end = pgn_end;
                
                *ret = addr;
                dbg(DBG_PRINT, "(GRADING3A)\n");
                return 0;
        }

        vma = vmmap_lookup(vmmap_cur, pagenum);

        vmmap_remove(vmmap_cur, vma->vma_start, vma->vma_end - vma->vma_start);
        *ret = addr;
        
        
        curproc->p_brk = addr;
        
        dbg(DBG_PRINT, "(GRADING3D 2)\n");
        return 0;
}
