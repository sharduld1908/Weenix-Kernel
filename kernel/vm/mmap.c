#include "globals.h"
#include "errno.h"
#include "types.h"

#include "mm/mm.h"
#include "mm/tlb.h"
#include "mm/mman.h"
#include "mm/page.h"

#include "proc/proc.h"

#include "util/string.h"
#include "util/debug.h"

#include "fs/vnode.h"
#include "fs/vfs.h"
#include "fs/file.h"

#include "vm/vmmap.h"
#include "vm/mmap.h"

/*
 * This function implements the mmap(2) syscall, but only
 * supports the MAP_SHARED, MAP_PRIVATE, MAP_FIXED, and
 * MAP_ANON flags.
 *
 * Add a mapping to the current process's address space.
 * You need to do some error checking; see the ERRORS section
 * of the manpage for the problems you should anticipate.
 * After error checking most of the work of this function is
 * done by vmmap_map(), but remember to clear the TLB.
 */
int
do_mmap(void *addr, size_t len, int prot, int flags,
        int fd, off_t off, void **ret)
{
        vmarea_t *mmap = NULL;
        file_t *fp = NULL;
        int retval;
        int lopage = 0;
        struct vnode *node = NULL;

        //check if valid length
        if(len <= 0 || len > (USER_MEM_HIGH - USER_MEM_LOW)){
                dbg(DBG_PRINT, "(GRADING3D 1)\n");
                return -EINVAL;
        }

        if (!(PAGE_ALIGNED(off)) || !(PAGE_ALIGNED(addr)))
        {
                dbg(DBG_PRINT, "(GRADING3D 1)\n");
                return -EINVAL;
        }

        if (flags ^ MAP_FIXED)
        {
                lopage = ADDR_TO_PN(addr);
                dbg(DBG_PRINT, "(GRADING3A)\n");
        }

        // Check for valid flags
        if (!((MAP_SHARED & flags) || (MAP_PRIVATE & flags))) {
                dbg(DBG_PRINT, "(GRADING3D 1)\n");
                return -EPERM;
        }

        if ((flags & MAP_FIXED) && addr == 0) {
                dbg(DBG_PRINT, "(GRADING3D 1)\n");
                return -EINVAL;
        }

        if (!(flags & MAP_ANON))
        {
                fp = fget(fd);
                if (fd < 0 || fd >= MAX_FILES || curproc->p_files[fd] == NULL || fp == NULL)
                {
                        dbg(DBG_PRINT, "(GRADING3D 1)\n");
                        return -EBADF;
                }

                if ((prot & PROT_WRITE) && ((fp->f_mode & FMODE_WRITE) != FMODE_WRITE))
                {
                        fput(fp);
                        dbg(DBG_PRINT, "(GRADING3D 1)\n");
                        return -EPERM;
                }
                fput(fp);
                node = fp->f_vnode;
                dbg(DBG_PRINT, "(GRADING3A)\n");
        }

        if((retval = vmmap_map(curproc->p_vmmap, node, lopage, (len - 1) / PAGE_SIZE + 1,
                               prot, flags, off, VMMAP_DIR_HILO, &mmap)) < 0){
                dbg(DBG_PRINT, "(GRADING3D 2)\n");
                return retval;
        }

        KASSERT(NULL != curproc->p_pagedir);
        dbg(DBG_PRINT, "(GRADING3A)\n");

        *ret = PN_TO_ADDR(mmap->vma_start);
        tlb_flush_all();
        dbg(DBG_PRINT, "(GRADING3A)\n");
        return retval;
}

/*
 * This function implements the munmap(2) syscall.
 *
 * As with do_mmap() it should perform the required error checking,
 * before calling upon vmmap_remove() to do most of the work.
 * Remember to clear the TLB.
 */
int
do_munmap(void *addr, size_t len)
{
        // Check for invalid input
        if (len <= 0 || addr == NULL) {
                dbg(DBG_PRINT, "(GRADING3A)\n");
                return -EINVAL;
        }

        // Check for valid address range
        if ((uint32_t)addr < USER_MEM_LOW || USER_MEM_HIGH - (uint32_t)addr < len) {
                dbg(DBG_PRINT, "(GRADING3D 1)\n");
                return -EINVAL;
        }

        // Check for page alignment
        if (!(PAGE_ALIGNED(addr))) {
                dbg(DBG_TEMP, "(GRADING3D) M14\n");
                return -EINVAL;
        }

        // Remove the mapping
        uint32_t npages = len / PAGE_SIZE;
        if (len % PAGE_SIZE != 0)
        {
                npages++;
                dbg(DBG_PRINT, "(GRADING3D 1)\n");
        }

        vmmap_remove(curproc->p_vmmap, ADDR_TO_PN(addr), npages);
        tlb_flush_all();
        dbg(DBG_PRINT, "(GRADING3D 1)\n");

        return 0;
}
