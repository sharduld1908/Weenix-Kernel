#include "kernel.h"
#include "errno.h"
#include "globals.h"

#include "vm/vmmap.h"
#include "vm/shadow.h"
#include "vm/anon.h"

#include "proc/proc.h"

#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"
#include "util/printf.h"

#include "fs/vnode.h"
#include "fs/file.h"
#include "fs/fcntl.h"
#include "fs/vfs_syscall.h"

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/mmobj.h"

static slab_allocator_t *vmmap_allocator;
static slab_allocator_t *vmarea_allocator;

void
vmmap_init(void)
{
        vmmap_allocator = slab_allocator_create("vmmap", sizeof(vmmap_t));
        KASSERT(NULL != vmmap_allocator && "failed to create vmmap allocator!");
        vmarea_allocator = slab_allocator_create("vmarea", sizeof(vmarea_t));
        KASSERT(NULL != vmarea_allocator && "failed to create vmarea allocator!");
}

vmarea_t *
vmarea_alloc(void)
{
        vmarea_t *newvma = (vmarea_t *) slab_obj_alloc(vmarea_allocator);
        if (newvma) {
                newvma->vma_vmmap = NULL;
        }
        return newvma;
}

void
vmarea_free(vmarea_t *vma)
{
        KASSERT(NULL != vma);
        slab_obj_free(vmarea_allocator, vma);
}

/* a debugging routine: dumps the mappings of the given address space. */
size_t
vmmap_mapping_info(const void *vmmap, char *buf, size_t osize)
{
        KASSERT(0 < osize);
        KASSERT(NULL != buf);
        KASSERT(NULL != vmmap);

        vmmap_t *map = (vmmap_t *)vmmap;
        vmarea_t *vma;
        ssize_t size = (ssize_t)osize;

        int len = snprintf(buf, size, "%21s %5s %7s %8s %10s %12s\n",
                           "VADDR RANGE", "PROT", "FLAGS", "MMOBJ", "OFFSET",
                           "VFN RANGE");

        list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink) {
                size -= len;
                buf += len;
                if (0 >= size) {
                        goto end;
                }

                len = snprintf(buf, size,
                               "%#.8x-%#.8x  %c%c%c  %7s 0x%p %#.5x %#.5x-%#.5x\n",
                               vma->vma_start << PAGE_SHIFT,
                               vma->vma_end << PAGE_SHIFT,
                               (vma->vma_prot & PROT_READ ? 'r' : '-'),
                               (vma->vma_prot & PROT_WRITE ? 'w' : '-'),
                               (vma->vma_prot & PROT_EXEC ? 'x' : '-'),
                               (vma->vma_flags & MAP_SHARED ? " SHARED" : "PRIVATE"),
                               vma->vma_obj, vma->vma_off, vma->vma_start, vma->vma_end);
        } list_iterate_end();

end:
        if (size <= 0) {
                size = osize;
                buf[osize - 1] = '\0';
        }
        
        return osize - size;
}

/* Create a new vmmap, which has no vmareas and does
 * not refer to a process. */
vmmap_t *
vmmap_create(void)
{

    // Allocate memory to new vmmap
    vmmap_t *vmm = (vmmap_t*) slab_obj_alloc(vmmap_allocator);
    
    // If the vmmap is not NULL, use it initialize its members
    if(vmm != NULL) {
        vmm->vmm_proc = NULL;
        list_init(&vmm->vmm_list);
        dbg(DBG_PRINT, "(GRADING3A)\n");
    }
    
    dbg(DBG_PRINT, "(GRADING3A)\n");
    return vmm;
}

/* Removes all vmareas from the address space and frees the
 * vmmap struct. */
void
vmmap_destroy(vmmap_t *map)
{


    KASSERT(NULL != map);
    dbg(DBG_PRINT, "(GRADING3A 3.a)\n");

    // Check if list is empty
    if (!list_empty(&map->vmm_list))
    {
        // Traverse through all vmareas and clean it up
        vmarea_t *vma = NULL;
        list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink)
        {
            // put operation on the mmobj in the vma
            mmobj_t *mmobj = vma->vma_obj;
            mmobj->mmo_ops->put(mmobj);

            // Check if the olink in vma is linked to other members in the list, then remove
            if (list_link_is_linked(&vma->vma_olink))
            {   
                list_remove(&vma->vma_olink);
                dbg(DBG_PRINT, "(GRADING3A)\n");
            }

            // Check if the plink in vma is linked to other members in the list, then remove
            if(list_link_is_linked(&vma->vma_plink)){
                list_remove(&vma->vma_plink);
                dbg(DBG_PRINT, "(GRADING3A)\n");
            }
            
            vmarea_free(vma);
            dbg(DBG_PRINT, "(GRADING3A)\n");
        } list_iterate_end();
        
        dbg(DBG_PRINT, "(GRADING3A)\n");
    }

    // Deallocate memory of the map
    slab_obj_free(vmmap_allocator, map);
    dbg(DBG_PRINT, "(GRADING3A)\n");
}

/* Add a vmarea to an address space. Assumes (i.e. asserts to some extent)
 * the vmarea is valid.  This involves finding where to put it in the list
 * of VM areas, and adding it. Don't forget to set the vma_vmmap for the
 * area. */
void
vmmap_insert(vmmap_t *map, vmarea_t *newvma)
{

    KASSERT(NULL != map && NULL != newvma);
    dbg(DBG_PRINT, "(GRADING3A 3.b)\n");
    
    KASSERT(NULL == newvma->vma_vmmap);
    dbg(DBG_PRINT, "(GRADING3A 3.b)\n");
    
    KASSERT(newvma->vma_start < newvma->vma_end);
    dbg(DBG_PRINT, "(GRADING3A 3.b)\n");
    
    KASSERT(ADDR_TO_PN(USER_MEM_LOW) <= newvma->vma_start && ADDR_TO_PN(USER_MEM_HIGH) >= newvma->vma_end);
    dbg(DBG_PRINT, "(GRADING3A 3.b)\n");
    
    vmarea_t *vma = NULL;
    list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink)
    {
        // Find the vma that starts after newvma ends
        if (vma->vma_start >= newvma->vma_end)
        {
            // Insert before the current vmarea
            list_insert_before(&vma->vma_plink, &newvma->vma_plink);
            newvma->vma_vmmap = map;
            dbg(DBG_PRINT, "(GRADING3A)\n");
            return;
        }

        dbg(DBG_PRINT, "(GRADING3A)\n");
    } list_iterate_end();

    // If we reach here, all the vmarea's end before the new vmarea starts, hence insert at tail
    list_insert_tail(&map->vmm_list, &newvma->vma_plink);
    newvma->vma_vmmap = map;
    dbg(DBG_PRINT, "(GRADING3A)\n");
}

/* Find a contiguous range of free virtual pages of length npages in
 * the given address space. Returns starting vfn for the range,
 * without altering the map. Returns -1 if no such range exists.
 *
 * Your algorithm should be first fit. If dir is VMMAP_DIR_HILO, you
 * should find a gap as high in the address space as possible; if dir
 * is VMMAP_DIR_LOHI, the gap should be as low as possible. */
int
vmmap_find_range(vmmap_t *map, uint32_t npages, int dir)
{

    uint32_t userspace_start = ADDR_TO_PN(USER_MEM_LOW);
    uint32_t userspace_end = ADDR_TO_PN(USER_MEM_HIGH);

    if(dir == VMMAP_DIR_HILO) 
    {
        vmarea_t *vma = NULL;
        list_iterate_reverse(&map->vmm_list, vma, vmarea_t, vma_plink)
        {
            // Find number of pages available between the vmarea's
            uint32_t pages = userspace_end - vma->vma_end;
            if (npages <= pages)
            {
                // Space is available
                dbg(DBG_PRINT, "(GRADING3A)\n");
                return (userspace_end - npages);
            }

            // Update userspace_end to the start of the curr vamrea
            userspace_end = vma->vma_start;
            dbg(DBG_PRINT, "(GRADING3D 1)\n");
        } list_iterate_end();

        // Check if space is available between the first vmarea and start of userspace
        if ((vma->vma_start - userspace_start) >= npages)
        {
            dbg(DBG_PRINT, "(GRADING3D 2)\n");
            return (vma->vma_start - npages);
        }

        // Space is not empty
        dbg(DBG_PRINT, "(GRADING3D 2)\n");
        return -1;
    }
    // commenting as LOHI never used
    // else 
    // {


    //     vmarea_t *vma = NULL;
    //     list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink)
    //     {
    //         // Find number of pages available between the vmarea's
    //         uint32_t pages = vma->vma_start - userspace_start;
    //         if (npages <= pages)
    //         {
    //             // Space is available
    //             dbg(DBG_PRINT, "(GRADING3A)\n");
    //             return userspace_start;
    //         }

    //         // Update userspace_end to the start of the curr vamrea
    //         userspace_start = vma->vma_end;
    //         dbg(DBG_PRINT, "(GRADING3D 1)\n");
    //     } list_iterate_end();

    //     // Check if space is available between the first vmarea and start of userspace
    //     dbg(DBG_PRINT, "(GRADING3D 2)\n");
    //     if ((userspace_end - vma->vma_end) >= npages)
    //     {
    //         dbg(DBG_PRINT, "(GRADING3D 2)\n");
    //         return vma->vma_end;
    //     }

    //     // Space is not empty
    //     dbg(DBG_PRINT, "(GRADING3D 2)\n");
    //     return -1;
    // }

    // Panic since dir should not be anything other than VMMAP_DIR_HILO or VMMAP_DIR_LOHI
    panic("Should not reach here!!! return added to avoid warning");
    return -1;

}

/* Find the vm_area that vfn lies in. Simply scan the address space
 * looking for a vma whose range covers vfn. If the page is unmapped,
 * return NULL. */
vmarea_t *
vmmap_lookup(vmmap_t *map, uint32_t vfn)
{

    KASSERT(NULL != map);
    dbg(DBG_PRINT, "(GRADING3A 3.c)\n");
    
    vmarea_t *curr_vma;
    list_iterate_begin(&map->vmm_list, curr_vma, vmarea_t, vma_plink)
    {
        if (vfn < curr_vma->vma_end && vfn >= curr_vma->vma_start)
        {
            dbg(DBG_PRINT, "(GRADING3A)\n");
            return curr_vma;
        }
        dbg(DBG_PRINT, "(GRADING3A)\n");
    } list_iterate_end();

    // No vmarea found
    dbg(DBG_PRINT, "(GRADING3D 1)\n");
    return NULL;
}

/* Allocates a new vmmap containing a new vmarea for each area in the
 * given map. The areas should have no mmobjs set yet. Returns pointer
 * to the new vmmap on success, NULL on failure. This function is
 * called when implementing fork(2). */
vmmap_t *
vmmap_clone(vmmap_t *map)
{
    
    vmarea_t *vma = NULL;
    vmmap_t *clone_vmm = vmmap_create();
    list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink)
    {
        // Allocate a new vmarea
        vmarea_t *new_vma = vmarea_alloc();

        // Copy everything for vmarea
        new_vma->vma_start = vma->vma_start;
        new_vma->vma_end = vma->vma_end;
        new_vma->vma_off = vma->vma_off;
        new_vma->vma_prot = vma->vma_prot;
        new_vma->vma_flags = vma->vma_flags;
        new_vma->vma_obj = NULL;

        list_link_init(&new_vma->vma_plink);
        list_link_init(&new_vma->vma_olink);

        vmmap_insert(clone_vmm, new_vma);
        dbg(DBG_PRINT, "(GRADING3A)\n");
    } list_iterate_end();
    
    // return the cloned vmmap
    dbg(DBG_PRINT, "(GRADING3A)\n");
    return clone_vmm;
}

/* Insert a mapping into the map starting at lopage for npages pages.
 * If lopage is zero, we will find a range of virtual addresses in the
 * process that is big enough, by using vmmap_find_range with the same
 * dir argument.  If lopage is non-zero and the specified region
 * contains another mapping that mapping should be unmapped.
 *
 * If file is NULL an anon mmobj will be used to create a mapping
 * of 0's.  If file is non-null that vnode's file will be mapped in
 * for the given range.  Use the vnode's mmap operation to get the
 * mmobj for the file; do not assume it is file->vn_obj. Make sure all
 * of the area's fields except for vma_obj have been set before
 * calling mmap.
 *
 * If MAP_PRIVATE is specified set up a shadow object for the mmobj.
 *
 * All of the input to this function should be valid (KASSERT!).
 * See mmap(2) for for description of legal input.
 * Note that off should be page aligned.
 *
 * Be very careful about the order operations are performed in here. Some
 * operation are impossible to undo and should be saved until there
 * is no chance of failure.
 *
 * If 'new' is non-NULL a pointer to the new vmarea_t should be stored in it.
 */
int
vmmap_map(vmmap_t *map, vnode_t *file, uint32_t lopage, uint32_t npages,
          int prot, int flags, off_t off, int dir, vmarea_t **new)
{
    
    KASSERT(NULL != map);                                                       
    dbg(DBG_PRINT, "(GRADING3A 3.d)\n");
    
    KASSERT(0 < npages);                                                        
    dbg(DBG_PRINT, "(GRADING3A 3.d)\n");

    KASSERT((MAP_SHARED & flags) || (MAP_PRIVATE & flags));                     
    dbg(DBG_PRINT, "(GRADING3A 3.d)\n");

    KASSERT((0 == lopage) || (ADDR_TO_PN(USER_MEM_LOW) <= lopage));            
    dbg(DBG_PRINT, "(GRADING3A 3.d)\n");

    KASSERT((0 == lopage) || (ADDR_TO_PN(USER_MEM_HIGH) >= (lopage + npages)));
    dbg(DBG_PRINT, "(GRADING3A 3.d)\n");

    KASSERT(PAGE_ALIGNED(off));
    dbg(DBG_PRINT, "(GRADING3A 3.d)\n");

    uint32_t addr_s = 0;
    vmarea_t *vma = vmarea_alloc();
    if (lopage == 0)
    {
        int avail_space_start = vmmap_find_range(map, npages, dir);
        if (avail_space_start < 0)
        {
            dbg(DBG_PRINT, "(GRADING3D 2)\n");
            return avail_space_start;
        }
        addr_s = avail_space_start;
        dbg(DBG_PRINT, "(GRADING3A)\n");
    }
    else
    {
        if (!vmmap_is_range_empty(map, lopage, npages))
        {
            vmmap_remove(map, lopage, npages);
            dbg(DBG_PRINT, "(GRADING3A)\n");
        }
        addr_s = lopage;
        dbg(DBG_PRINT, "(GRADING3A)\n");
    }

    // Setup members of the vmarea
    vma->vma_start = addr_s;
    vma->vma_end = addr_s + npages;
    vma->vma_off = ADDR_TO_PN(off);
    vma->vma_prot = prot;
    vma->vma_flags = flags;
    list_link_init(&vma->vma_plink);
    list_link_init(&vma->vma_olink);

    if (file)
    {
        // Map the file to vmarea
        int v_mmap = file->vn_ops->mmap(file, vma, &vma->vma_obj);
        dbg(DBG_PRINT, "(GRADING3A)\n");
    }
    else
    {
        // Map an anon object to vmarea
        mmobj_t *anon_obj = NULL;
        anon_obj = anon_create();
        vma->vma_obj = anon_obj;
        dbg(DBG_PRINT, "(GRADING3A)\n");
    }

    list_insert_tail(mmobj_bottom_vmas(vma->vma_obj), &vma->vma_olink);

    // shadow case
    if ((flags & MAP_PRIVATE) == MAP_PRIVATE)
    {
        mmobj_t *shadow_obj = vma->vma_obj;
        vma->vma_obj = shadow_create();
        vma->vma_obj->mmo_shadowed = shadow_obj;
        vma->vma_obj->mmo_un.mmo_bottom_obj = mmobj_bottom_obj(shadow_obj);
        dbg(DBG_PRINT, "(GRADING3A)\n");
    }

    vmmap_insert(map, vma);

    if (new != NULL)
    {
        *new = vma;
        dbg(DBG_PRINT, "(GRADING3A)\n");
    }

    dbg(DBG_PRINT, "(GRADING3A)\n");
    return 0;
}

/*
 * We have no guarantee that the region of the address space being
 * unmapped will play nicely with our list of vmareas.
 *
 * You must iterate over each vmarea that is partially or wholly covered
 * by the address range [addr ... addr+len). The vm-area will fall into one
 * of four cases, as illustrated below:
 *
 * key:
 *          [             ]   Existing VM Area
 *        *******             Region to be unmapped
 *
 * Case 1:  [   ******    ]
 * The region to be unmapped lies completely inside the vmarea. We need to
 * split the old vmarea into two vmareas. be sure to increment the
 * reference count to the file associated with the vmarea.
 *
 * Case 2:  [      *******]**
 * The region overlaps the end of the vmarea. Just shorten the length of
 * the mapping.
 *
 * Case 3: *[*****        ]
 * The region overlaps the beginning of the vmarea. Move the beginning of
 * the mapping (remember to update vma_off), and shorten its length.
 *
 * Case 4: *[*************]**
 * The region completely contains the vmarea. Remove the vmarea from the
 * list.
 */
int
vmmap_remove(vmmap_t *map, uint32_t lopage, uint32_t npages) 
{
    vmarea_t *vma_curr = NULL;
    
    // init
    uint32_t start_vfn = lopage;
    uint32_t end_vfn = lopage + npages;
    
    // iterate theough the list
    list_iterate_begin(&map->vmm_list, vma_curr, vmarea_t, vma_plink)
    {
        int flg_overlap = 0;
        if (start_vfn > vma_curr->vma_end || end_vfn < vma_curr->vma_start)
        {
            flg_overlap = 1;
            dbg(DBG_PRINT, "(GRADING3D 1)\n");
        }

        if (start_vfn >= vma_curr->vma_start && !flg_overlap && end_vfn <= vma_curr->vma_end)
        {
            vmarea_t *split_vma = vmarea_alloc();

            split_vma->vma_flags = vma_curr->vma_flags;
            split_vma->vma_prot = vma_curr->vma_prot;

            list_link_init(&split_vma->vma_plink);
            list_link_init(&split_vma->vma_olink);
            
            split_vma->vma_obj = vma_curr->vma_obj;
            split_vma->vma_obj->mmo_ops->ref(split_vma->vma_obj);

            split_vma->vma_start = vma_curr->vma_start;
            split_vma->vma_end = start_vfn;
            split_vma->vma_vmmap = map;
            split_vma->vma_off = vma_curr->vma_off;
            
            vma_curr->vma_off = vma_curr->vma_off + end_vfn - vma_curr->vma_start;
            vma_curr->vma_start = end_vfn;

            if (split_vma->vma_start == split_vma->vma_end)
            {
                split_vma->vma_obj->mmo_ops->put(split_vma->vma_obj);

                vmarea_free(split_vma);
                split_vma = NULL;
                dbg(DBG_PRINT, "(GRADING3A)\n");
            }
            else
            {
                list_insert_before(&vma_curr->vma_plink, &split_vma->vma_plink);
                dbg(DBG_PRINT, "(GRADING3D 2)\n");
            }

            if (vma_curr->vma_start == vma_curr->vma_end)
            {
                vma_curr->vma_obj->mmo_ops->put(vma_curr->vma_obj);
                if (list_link_is_linked(&vma_curr->vma_olink))
                {
                    list_remove(&vma_curr->vma_olink);
                    dbg(DBG_PRINT, "(GRADING3A)\n");
                }

                if (list_link_is_linked(&vma_curr->vma_plink))
                {
                    list_remove(&vma_curr->vma_plink);
                    dbg(DBG_PRINT, "(GRADING3A)\n");
                }
                
                vmarea_free(vma_curr);
                vma_curr = NULL;
                dbg(DBG_PRINT, "(GRADING3A)\n");
            }
            pt_unmap_range(curproc->p_pagedir, (uintptr_t)PN_TO_ADDR(start_vfn), (uintptr_t)PN_TO_ADDR(end_vfn));
            dbg(DBG_PRINT, "(GRADING3A)\n");
            break;
        }
        else if (vma_curr->vma_end < end_vfn  && vma_curr->vma_start <= start_vfn && !flg_overlap)
        {
            vma_curr->vma_end = lopage;

            if (vma_curr->vma_start == vma_curr->vma_end)
            {
                vma_curr->vma_obj->mmo_ops->put(vma_curr->vma_obj);
                if (list_link_is_linked(&vma_curr->vma_olink))
                {
                    list_remove(&vma_curr->vma_olink);
                    dbg(DBG_PRINT, "(GRADING3A)\n");
                }
                if (list_link_is_linked(&vma_curr->vma_plink))
                {
                    list_remove(&vma_curr->vma_plink);
                    dbg(DBG_PRINT, "(GRADING3A)\n");
                }
                
                vmarea_free(vma_curr);
                vma_curr = NULL;
                dbg(DBG_PRINT, "(GRADING3A)\n");
            }
            dbg(DBG_PRINT, "(GRADING3A)\n");
        }
        else if (start_vfn < vma_curr->vma_start && end_vfn <= vma_curr->vma_end && !flg_overlap)
        {
            vma_curr->vma_off = vma_curr->vma_off + end_vfn - vma_curr->vma_start;
            vma_curr->vma_start = end_vfn;

            if (vma_curr->vma_start == vma_curr->vma_end)
            {
                vma_curr->vma_obj->mmo_ops->put(vma_curr->vma_obj);
                if (list_link_is_linked(&vma_curr->vma_olink))
                {
                    list_remove(&vma_curr->vma_olink);
                    dbg(DBG_PRINT, "(GRADING3D 2)\n");
                }
                if (list_link_is_linked(&vma_curr->vma_plink))
                {
                    list_remove(&vma_curr->vma_plink);
                    dbg(DBG_PRINT, "(GRADING3D 41)\n");
                }
                
                vmarea_free(vma_curr);
                vma_curr = NULL;
                dbg(DBG_PRINT, "(GRADING3D 2)\n");
            }

            dbg(DBG_PRINT, "(GRADING3D 1)\n");
        }
        else if (start_vfn < vma_curr->vma_start && end_vfn > vma_curr->vma_end && !flg_overlap)
        {

            uint32_t start_addr = (uint32_t)PN_TO_ADDR(vma_curr->vma_start);
            uint32_t end_addr = (uint32_t)PN_TO_ADDR(vma_curr->vma_end);

            vma_curr->vma_obj->mmo_ops->put(vma_curr->vma_obj);
            if (list_link_is_linked(&vma_curr->vma_olink))
            {
                list_remove(&vma_curr->vma_olink);
                dbg(DBG_PRINT, "(GRADING3D 2)\n");
            }
            if (list_link_is_linked(&vma_curr->vma_plink))
            {
                list_remove(&vma_curr->vma_plink);
                dbg(DBG_PRINT, "(GRADING3D 2)\n");
            }
            
            vmarea_free(vma_curr);
            vma_curr = NULL;

            // unmap
            pt_unmap_range(curproc->p_pagedir, start_addr, end_addr);
            dbg(DBG_PRINT, "(GRADING3D 2)\n");
        }

        dbg(DBG_PRINT, "(GRADING3A)\n");
    } list_iterate_end();
    
    dbg(DBG_PRINT, "(GRADING3A)\n");
    return 0;
}

/*
 * Returns 1 if the given address space has no mappings for the
 * given range, 0 otherwise.
 */
int
vmmap_is_range_empty(vmmap_t *map, uint32_t startvfn, uint32_t npages)
{
    
    uint32_t endvfn = startvfn + npages;
    KASSERT((startvfn < endvfn) && (ADDR_TO_PN(USER_MEM_LOW) <= startvfn) && (ADDR_TO_PN(USER_MEM_HIGH) >= endvfn));
    dbg(DBG_PRINT, "(GRADING3A 3.e)\n");

    vmarea_t *curr_vma = NULL;
    list_iterate_begin(&map->vmm_list, curr_vma, vmarea_t, vma_plink)
    {
        if (curr_vma->vma_end > startvfn && curr_vma->vma_start < endvfn)
        {
            dbg(DBG_PRINT, "(GRADING3A)\n");
            return 0;
        }
        else if (startvfn < curr_vma->vma_start && curr_vma->vma_start >= endvfn)
        {
            dbg(DBG_PRINT, "(GRADING3A)\n");
            return 1;
        }
    } list_iterate_end();

    dbg(DBG_PRINT, "(GRADING3A)\n");
    return 1;
}

/* Read into 'buf' from the virtual address space of 'map' starting at
 * 'vaddr' for size 'count'. To do so, you will want to find the vmareas
 * to read from, then find the pframes within those vmareas corresponding
 * to the virtual addresses you want to read, and then read from the
 * physical memory that pframe points to. You should not check permissions
 * of the areas. Assume (KASSERT) that all the areas you are accessing exist.
 * Returns 0 on success, -errno on error.
 */
uint32_t min_uint32(uint32_t a, uint32_t b) {
    dbg(DBG_PRINT, "(GRADING3A)\n");
    return (a < b) ? a : b;
}

int
vmmap_read(vmmap_t *map, const void *vaddr, void *buf, size_t count)
{
    uint32_t vfn = ADDR_TO_PN(vaddr);
    const void *addr_s = vaddr;
    size_t read_bytes = 0;

    while (read_bytes < count)
    {
        vmarea_t *vma = vmmap_lookup(map, vfn);

        uint32_t pagenum = vfn - (vma->vma_start + vma->vma_off);
        uint32_t start_pg_off = PAGE_OFFSET(addr_s);
        
        pframe_t *pf = NULL;
        int lookup_ret = pframe_lookup(vma->vma_obj, pagenum, 0, &pf);

        uint32_t read_size = min_uint32((count - read_bytes) ,(PAGE_SIZE - start_pg_off));

        memcpy((void*)((uint32_t)buf + (uint32_t)read_bytes), (void*)((uint32_t)pf->pf_addr + (uint32_t)start_pg_off), read_size);
        read_bytes = read_bytes + read_size;
        
        addr_s = (void*)((uint32_t)addr_s + (uint32_t)read_size);
        vfn = ADDR_TO_PN(addr_s);
        
        dbg(DBG_PRINT, "(GRADING3A)\n");
    }

    dbg(DBG_PRINT, "(GRADING3A)\n");
    return 0;
}

/* Write from 'buf' into the virtual address space of 'map' starting at
 * 'vaddr' for size 'count'. To do this, you will need to find the correct
 * vmareas to write into, then find the correct pframes within those vmareas,
 * and finally write into the physical addresses that those pframes correspond
 * to. You should not check permissions of the areas you use. Assume (KASSERT)
 * that all the areas you are accessing exist. Remember to dirty pages!
 * Returns 0 on success, -errno on error.
 */
int
vmmap_write(vmmap_t *map, void *vaddr, const void *buf, size_t count)
{   
    uint32_t vfn = ADDR_TO_PN(vaddr);
    size_t write_bytes_done = 0;
    const void *addr_s = vaddr;

    while (write_bytes_done < count)
    {
        
        vmarea_t *vma = vmmap_lookup(map, vfn);
        
        uint32_t pagenum = vfn - vma->vma_start + vma->vma_off;
        uint32_t start_pg_off = PAGE_OFFSET(addr_s);
        pframe_t *page_frame;

        int lookup_ret = pframe_lookup(vma->vma_obj, pagenum, 1, &page_frame);

        uint32_t write_size = count - write_bytes_done;
        write_size = write_size > PAGE_SIZE - start_pg_off ? PAGE_SIZE - start_pg_off : write_size;

        memcpy((char *)((uint32_t)page_frame->pf_addr + (uint32_t)start_pg_off), (char *)((uint32_t)buf + (uint32_t)write_bytes_done), write_size);

        int dirty_frame = pframe_dirty(page_frame);

        write_bytes_done = write_bytes_done + write_size;
        addr_s = (void *)((uint32_t)addr_s + (uint32_t)write_size);
        vfn = ADDR_TO_PN(addr_s);

        dbg(DBG_PRINT, "(GRADING3A)\n");
    }
    
    dbg(DBG_PRINT, "(GRADING3A)\n");
    return 0;
}
