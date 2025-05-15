#include "kernel.h"
#include "util/init.h"
#include "util/string.h"
#include "util/printf.h"
#include "errno.h"
#include "fs/stat.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "mm/slab.h"
#include "proc/sched.h"
#include "util/debug.h"
#include "vm/vmmap.h"
#include "globals.h"

/* Related to vnodes representing special files: */
void init_special_vnode(vnode_t *vn);
int special_file_read(vnode_t *file, off_t offset, void *buf, size_t count);
int special_file_write(vnode_t *file, off_t offset, const void *buf, size_t count);
int special_file_mmap(vnode_t *file, vmarea_t *vma, mmobj_t **ret);
int special_file_stat(vnode_t *vnode, struct stat *ss);
int special_file_fillpage(vnode_t *file, off_t offset, void *pagebuf);
int special_file_dirtypage(vnode_t *file, off_t offset);
int special_file_cleanpage(vnode_t *file, off_t offset, void *pagebuf);

/* vnode operations tables for special files: */
static vnode_ops_t bytedev_spec_vops = {
        .read = special_file_read,
        .write = special_file_write,
        .mmap = special_file_mmap,
        .create = NULL,
        .mknod = NULL,
        .lookup = NULL,
        .link = NULL,
        .unlink = NULL,
        .mkdir = NULL,
        .rmdir = NULL,
        .readdir = NULL,
        .stat = special_file_stat,
        .fillpage = special_file_fillpage,
        .dirtypage = special_file_dirtypage,
        .cleanpage = special_file_cleanpage
};

static vnode_ops_t blockdev_spec_vops = {
        .read = NULL,
        .write = NULL,
        .mmap = NULL,
        .create = NULL,
        .mknod = NULL,
        .lookup = NULL,
        .link = NULL,
        .unlink = NULL,
        .mkdir = NULL,
        .rmdir = NULL,
        .readdir = NULL,
        .stat = special_file_stat,
        .fillpage = NULL,
        .dirtypage = NULL,
        .cleanpage = NULL
};

void
init_special_vnode(vnode_t *vn)
{
        if (S_ISCHR(vn->vn_mode)) {
                vn->vn_ops = &bytedev_spec_vops;
                vn->vn_cdev = bytedev_lookup(vn->vn_devid);
        } else {
                KASSERT(S_ISBLK(vn->vn_mode));
                vn->vn_ops = &blockdev_spec_vops;
                vn->vn_bdev = blockdev_lookup(vn->vn_devid);
        }
}

/* Stat is currently the only filesystem specific routine that we have to worry
 * about for special files.  Here we just call the stat routine for the root
 * directory of the filesystem.
 */
int
special_file_stat(vnode_t *vnode, struct stat *ss)
{
        KASSERT(vnode->vn_fs->fs_root->vn_ops->stat != NULL);

        /* call the containing file system's stat routine */
        return vnode->vn_fs->fs_root->vn_ops->stat(vnode, ss);
}


/*
 * If the file is a byte device then find the file's
 * bytedev_t, and call read on it. Return what read returns.
 *
 * If the file is a block device then return -ENOTSUP
 */
int
special_file_read(vnode_t *file, off_t offset, void *buf, size_t count)
{
        KASSERT(file);
        
        KASSERT((S_ISCHR(file->vn_mode) || S_ISBLK(file->vn_mode)));
        
        KASSERT(file->vn_cdev && file->vn_cdev->cd_ops && file->vn_cdev->cd_ops->read);
         
        return file->vn_cdev->cd_ops->read(file->vn_cdev,(int)offset,buf,(int)count);

}

/*
 * If the file is a byte device find the file's
 * bytedev_t, and call its write. Return what write returns.
 *
 * If the file is a block device then return -ENOTSUP.
 */
int
special_file_write(vnode_t *file, off_t offset, const void *buf, size_t count)
{
        KASSERT(file);
        
        KASSERT((S_ISCHR(file->vn_mode) || S_ISBLK(file->vn_mode)));
        
        KASSERT(file->vn_cdev && file->vn_cdev->cd_ops && file->vn_cdev->cd_ops->write);
               
        return file->vn_cdev->cd_ops->write(file->vn_cdev,(int)offset,buf,(int)count);
}

/* Memory map the special file represented by <file>. All of the
 * work for this function is device-specific, so look up the
 * file's bytedev_t and pass the arguments through to its mmap
 * function. Return what that function returns.
 *
 * Do not worry about this until VM.
 */
int
special_file_mmap(vnode_t *file, vmarea_t *vma, mmobj_t **ret)
{
        dbg(DBG_PRINT, "(GRADING3A)\n");
        return file->vn_cdev->cd_ops->mmap(file,vma,ret);
}

/* Just as with mmap above, pass the call through to the
 * device-specific fillpage function.
 *
 * Do not worry about this until VM.
 */
int
special_file_fillpage(vnode_t *file, off_t offset, void *pagebuf)
{
        NOT_YET_IMPLEMENTED("VM: special_file_fillpage");
        return 0;
}

/* Just as with mmap above, pass the call through to the
 * device-specific dirtypage function.
 *
 * Do not worry about this until VM.
 */
int
special_file_dirtypage(vnode_t *file, off_t offset)
{
        NOT_YET_IMPLEMENTED("VM: special_file_dirtypage");
        return 0;
}

/* Just as with mmap above, pass the call through to the
 * device-specific cleanpage function.
 *
 * Do not worry about this until VM.
 */
int
special_file_cleanpage(vnode_t *file, off_t offset, void *pagebuf)
{
        NOT_YET_IMPLEMENTED("VM: special_file_cleanpage");
        return 0;
}
