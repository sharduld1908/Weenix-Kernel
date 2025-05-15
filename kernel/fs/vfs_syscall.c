#include "kernel.h"
#include "errno.h"
#include "globals.h"
#include "fs/vfs.h"
#include "fs/file.h"
#include "fs/vnode.h"
#include "fs/vfs_syscall.h"
#include "fs/open.h"
#include "fs/fcntl.h"
#include "fs/lseek.h"
#include "mm/kmalloc.h"
#include "util/string.h"
#include "util/printf.h"
#include "fs/stat.h"
#include "util/debug.h"

/*
 * Syscalls for vfs. Refer to comments or man pages for implementation.
 * Do note that you don't need to set errno, you should just return the
 * negative error code.
 */

/* To read a file:
 *      o fget(fd)
 *      o call its virtual read vn_op
 *      o update f_pos
 *      o fput() it
 *      o return the number of bytes read, or an error
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not a valid file descriptor or is not open for reading.
 *      o EISDIR
 *        fd refers to a directory.
 *
 * In all cases, be sure you do not leak file refcounts by returning before
 * you fput() a file that you fget()'ed.
 */
int
do_read(int fd, void *buf, size_t nbytes)
{
        file_t *file_to_read;
        file_to_read = fget(fd);

        if(file_to_read){
                
                // not a valid mode 
                if(file_to_read->f_mode == 0)
                {
                        fput(file_to_read);
                        return -EBADF;
                }
                
                if(file_to_read->f_mode == FMODE_WRITE || file_to_read->f_mode == (FMODE_WRITE & FMODE_APPEND)){
                        fput(file_to_read);
                        return -EBADF;
                }
                if(S_ISDIR(file_to_read->f_vnode->vn_mode)){
                        fput(file_to_read);
                        return -EISDIR;
                }
                
                int res = file_to_read->f_vnode->vn_ops->read(file_to_read->f_vnode,file_to_read->f_pos,buf,nbytes);
                if(res > 0){
                        file_to_read->f_pos += res;
                }

                fput(file_to_read);
                return res;
        }
        else {
                return -EBADF;
        }

        panic("Should never get here!\n");
}

/* Very similar to do_read.  Check f_mode to be sure the file is writable.  If
 * f_mode & FMODE_APPEND, do_lseek() to the end of the file, call the write
 * vn_op, and fput the file.  As always, be mindful of refcount leaks.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not a valid file descriptor or is not open for writing.
 */
int
do_write(int fd, const void *buf, size_t nbytes)
{
        file_t *file_to_write;
        file_to_write = fget(fd);

        if(file_to_write){

                if(file_to_write->f_mode == 0)
                {
                        fput(file_to_write);
                        return -EBADF;
                }

                // if(S_ISDIR(file_to_write->f_vnode->vn_mode)){
                //         fput(file_to_write);
                //         return -EISDIR;
                // }

                if(file_to_write->f_mode & FMODE_APPEND){
                        do_lseek(fd, 0, SEEK_END);
                }
                else if(file_to_write->f_mode & FMODE_WRITE) {
                        do_lseek(fd, 0, SEEK_CUR);
                }
                else{
                        fput(file_to_write);
                        return -EBADF;
                }

                int res = file_to_write->f_vnode->vn_ops->write(file_to_write->f_vnode, file_to_write->f_pos, buf, nbytes);
                if(res > 0) {
                        file_to_write->f_pos += res;
                
                        KASSERT((S_ISCHR(file_to_write->f_vnode->vn_mode)) ||
                                (S_ISBLK(file_to_write->f_vnode->vn_mode)) ||
                                ((S_ISREG(file_to_write->f_vnode->vn_mode)) && (file_to_write->f_pos <= file_to_write->f_vnode->vn_len)));
                }

                fput(file_to_write);
                return res;
        }
        else{
                return -EBADF;
        }

        panic("Should never get here!\n");
}

/*
 * Zero curproc->p_files[fd], and fput() the file. Return 0 on success
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd isn't a valid open file descriptor.
 */
int
do_close(int fd)
{
        file_t *fileToBeClosed = fget(fd);
        if(fileToBeClosed == NULL)
        {
                // fd isn't an open file descriptor.
                return -EBADF;
        }
        // invalid file
        if(fileToBeClosed->f_mode == 0)
        {
                fput(fileToBeClosed);
                return -EBADF;
        }

        // calling fput for the fget above (as we won't be using fileToBeClosed after we have returned from this function)
        fput(fileToBeClosed);
        
        
        curproc->p_files[fd] = NULL;
        // as the current process is not pointing to it (from the above statement) - we again call vput AND fput on it
        // vput(fileToBeClosed->f_vnode); - DON'T NEED TO CALL THIS EXPLICITLY, the below fput handles 
        fput(fileToBeClosed);

        return 0;
}

/* To dup a file:
 *      o fget(fd) to up fd's refcount
 *      o get_empty_fd()
 *      o point the new fd to the same file_t* as the given fd
 *      o return the new file descriptor
 *
 * Don't fput() the fd unless something goes wrong.  Since we are creating
 * another reference to the file_t*, we want to up the refcount.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd isn't an open file descriptor.
 *      o EMFILE
 *        The process already has the maximum number of file descriptors open
 *        and tried to open a new one.
 */
int
do_dup(int fd)
{
        // Look in process fd table and return the file
        file_t *fileToBeDuplicated = fget(fd);

        if(fileToBeDuplicated == NULL)
        {
                // fd isn't an open file descriptor.
                return -EBADF;
        }
        if(fileToBeDuplicated->f_mode == 0)
        {
                fput(fileToBeDuplicated);
                return -EBADF;
        }

        int next_available_new_fd = get_empty_fd(curproc);
        // if(next_available_new_fd == -EMFILE)
        // {
        //         // maximum number of file descriptors open
        //         fput(fileToBeDuplicated);
        //         return -EMFILE;
        // }

        curproc->p_files[next_available_new_fd] = fileToBeDuplicated;
        return next_available_new_fd;
}

/* Same as do_dup, but insted of using get_empty_fd() to get the new fd,
 * they give it to us in 'nfd'.  If nfd is in use (and not the same as ofd)
 * do_close() it first.  Then return the new file descriptor.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        ofd isn't an open file descriptor, or nfd is out of the allowed
 *        range for file descriptors.
 */
int
do_dup2(int ofd, int nfd)
{

        if (ofd < 0 || ofd >= MAX_FILES || nfd < 0 || nfd >= MAX_FILES)
        {
                return -EBADF;
        }

        file_t *fileToBeDuplicated = fget(ofd);

        if(fileToBeDuplicated == NULL)
        {
                // ofd isn't an open file descriptor
                return -EBADF;
        }
        if(fileToBeDuplicated->f_mode == 0)
        {
                fput(fileToBeDuplicated);
                return -EBADF;
        }

        // nfd
        // If nfd is in use (and not the same as ofd)
        // if(curproc->p_files[nfd] != NULL && (nfd != ofd))
        // {
        //         do_close(nfd);
        //         curproc->p_files[nfd] = fileToBeDuplicated;
        //         return nfd;
        // }

        /* dup2-ing a file to itself works */
        if(curproc->p_files[nfd] != NULL && (nfd == ofd))
        {
                // syscall_success(fd2 = dup2(fd1, fd1));
                // because we have the same fd, it points to the same file so need to fput  from previous fget
                fput(fileToBeDuplicated);
                return nfd;
        }

        // nfd not is use
        if(curproc->p_files[nfd] == NULL)
        {
                curproc->p_files[nfd] = fileToBeDuplicated;
                return nfd;
        }

        // panic("Should not react here!!! Below code is added to avoid getting a compiler warning!!!\n");
        return nfd; 
        
}

/*
 * This routine creates a special file of the type specified by 'mode' at
 * the location specified by 'path'. 'mode' should be one of S_IFCHR or
 * S_IFBLK (you might note that mknod(2) normally allows one to create
 * regular files as well-- for simplicity this is not the case in Weenix).
 * 'devid', as you might expect, is the device identifier of the device
 * that the new special file should represent.
 *
 * You might use a combination of dir_namev, lookup, and the fs-specific
 * mknod (that is, the containing directory's 'mknod' vnode operation).
 * Return the result of the fs-specific mknod, or an error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL
 *        mode requested creation of something other than a device special
 *        file.
 *      o EEXIST
 *        path already exists.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_mknod(const char *path, int mode, unsigned devid)
{
        // if(!(S_ISBLK(mode) || S_ISCHR(mode))){
        //         return -EINVAL;
        // }

        int retval;
        size_t namelen = 0;
        const char *name = NULL;
        vnode_t *res_vnode = NULL;
        struct vnode *base = NULL;

        retval = dir_namev(path, &namelen, &name, base, &res_vnode);
        // if(retval < 0){
        //         return retval;
        // }

        KASSERT(NULL != res_vnode->vn_ops->mknod);

        vnode_t *lookup_result = NULL;
        retval = lookup(res_vnode,name,namelen,&lookup_result);

        if(retval == -ENOENT){
                retval = res_vnode->vn_ops->mknod(res_vnode,name,namelen,mode,devid);
                vput(res_vnode);
                return retval;
        }
        // else if(retval >= 0){
        //         //file already exists
        //         //todo: check if this is tested anywhere
        //         vput(lookup_result);
        //         vput(res_vnode);
        //         return -EEXIST;
        // }
        // else{
        //         vput(res_vnode);
        //         return retval;
        // }

        panic("Should not react here!!! Below code is added to avoid getting a compiler warning!!!\n");
        return 0; 
        
}

/* Use dir_namev() to find the vnode of the dir we want to make the new
 * directory in.  Then use lookup() to make sure it doesn't already exist.
 * Finally call the dir's mkdir vn_ops. Return what it returns.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EEXIST
 *        path already exists.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_mkdir(const char *path)
{
        //INFO: dir_namev finds the parent directory, namelen, name and parent_directory are "out" arguments
        int retval;
        vnode_t *parent_directory = NULL;
        size_t namelen = 0;
        const char *name = NULL;
        struct vnode *base = NULL;

        retval = dir_namev(path, &namelen, &name, base, &parent_directory);
        if(retval < 0){
                return retval;
        }

        if (namelen > NAME_LEN)
        {
                vput(parent_directory);
                return -ENAMETOOLONG;
        }

        if(namelen == 0){
                vput(parent_directory);
                return -EEXIST;
        }

        if(!S_ISDIR(parent_directory->vn_mode)) {
                vput(parent_directory);
                return -ENOTDIR;
        }

        KASSERT(NULL != parent_directory->vn_ops->mkdir);

        vnode_t *lookup_result = NULL;
        if((retval = lookup(parent_directory,name,namelen,&lookup_result)) >= 0){
                //file already exists
                vput(lookup_result);
                vput(parent_directory);
                return -EEXIST;
        }
        else if(retval == -ENOENT){
                //no file found create file
                retval = parent_directory->vn_ops->mkdir(parent_directory,name,namelen);
                vput(parent_directory);
                return retval;
        }
        // else{
        //         //any other error returned from lookup
        //         vput(parent_directory);
        //         return retval;
        // }

        panic("Should not react here!!! Below code is added to avoid getting a compiler warning!!!\n");
        return 0; 
        
}

/* Use dir_namev() to find the vnode of the directory containing the dir to be
 * removed. Then call the containing dir's rmdir v_op.  The rmdir v_op will
 * return an error if the dir to be removed does not exist or is not empty, so
 * you don't need to worry about that here. Return the value of the v_op,
 * or an error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL
 *        path has "." as its final component.
 *      o ENOTEMPTY
 *        path has ".." as its final component.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_rmdir(const char *path)
{
        size_t namelen = 0;
        const char *name = NULL;
        vnode_t *res_vnode = NULL;
        vnode_t *base = NULL;

        int retval;

        if((retval = dir_namev(path,&namelen,&name,base,&res_vnode)) < 0){
                return retval;
        }
        if(!S_ISDIR(res_vnode->vn_mode)){
                vput(res_vnode);
                return -ENOTDIR;
        }

        if(name_match(".", name, namelen)) {
                vput(res_vnode);
                return -EINVAL;
        }
        if(name_match("..", name, namelen)) {
                vput(res_vnode);
                return -ENOTEMPTY;
        }

        KASSERT(NULL != res_vnode->vn_ops->rmdir);
        retval = res_vnode->vn_ops->rmdir(res_vnode,name,namelen);
        vput(res_vnode);

        return retval;
}

/*
 * Similar to do_rmdir, but for files.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EPERM
 *        path refers to a directory.
 *      o ENOENT
 *        Any component in path does not exist, including the element at the
 *        very end.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_unlink(const char *path)
{
        size_t  namelen = 0;
        const char *name;
        vnode_t *parent_dir = NULL;
        vnode_t *base = NULL;

        int retval;
        retval = dir_namev(path,&namelen,&name,base,&parent_dir);
        // if((retval = dir_namev(path,&namelen,&name,base,&parent_dir)) < 0) {
        //         return retval;
        // }
        if (retval != 0)
        {

                return retval;
        }

        vnode_t *file_to_delete;
        retval = lookup(parent_dir,name,namelen,&file_to_delete);
        if(retval < 0){
                vput(parent_dir);
                return retval;
        }
        if(S_ISDIR(file_to_delete->vn_mode)){
                vput(parent_dir);
                vput(file_to_delete);
                return -EPERM;
        }

        KASSERT(NULL != parent_dir->vn_ops->unlink);

        retval = parent_dir->vn_ops->unlink(parent_dir,name,namelen);
        vput(parent_dir);
        vput(file_to_delete);

        return retval;
}

/* To link:
 *      o open_namev(from)
 *      o dir_namev(to)
 *      o call the destination dir's (to) link vn_ops.
 *      o return the result of link, or an error
 *
 * Remember to vput the vnodes returned from open_namev and dir_namev.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EEXIST
 *        to already exists.
 *      o ENOENT
 *        A directory component in from or to does not exist.
 *      o ENOTDIR
 *        A component used as a directory in from or to is not, in fact, a
 *        directory.
 *      o ENAMETOOLONG
 *        A component of from or to was too long.
 *      o EPERM
 *        from is a directory.
 */
int
do_link(const char *from, const char *to)
{

        vnode_t* from_vnode = NULL;
        vnode_t* base = NULL;

        // create vnode for the new file (manually need to create the file too?)
        // in sir example, as I understand - creates the image vnode (in /etc directory)
        int retval = open_namev(from, O_CREAT, &from_vnode, base);
        if(retval != 0)
        {
                return retval;
        }
        if(S_ISDIR(from_vnode->vn_mode)) {
                vput(from_vnode);
                return -EPERM;
        }


        // test if the to directory exits
        // int retval;
        size_t namelen = 0;
        const char *name = NULL;
        vnode_t *to_parent_vnode = NULL;
        struct vnode *base_2 = NULL;

        // getting the to's parent's vnode at parent_to_vnode below
        retval = dir_namev(to, &namelen, &name, base_2, &to_parent_vnode);
        if(retval != 0){
                vput(from_vnode);
                return retval;
        }
        if (namelen > NAME_LEN)
        {

                vput(from_vnode);
                vput(to_parent_vnode);
                return -ENAMETOOLONG;
        }
        // I think should also do lookup to get the actual to folder's vnode

        // KASSERT(NULL != res_vnode->vn_ops->mknod);

        vnode_t *to_vnode = NULL;
        retval = lookup(to_parent_vnode,name,namelen,&to_vnode);
        if (to_vnode && retval == 0)
        {

                vput(from_vnode);
                vput(to_parent_vnode);
                vput(to_vnode);
                return -EEXIST;
        }

        // if(retval == -ENOENT || retval == -ENOTDIR){
        //         vput(from_vnode);
        //         vput(to_parent_vnode);
        //         return retval;
        // }
        // else if(retval != -EEXIST){
        //         vput(from_vnode);
        //         vput(to_vnode);
        //         vput(to_parent_vnode);
        //         return retval;
        // }
        // else{

                retval = to_parent_vnode->vn_ops->link(from_vnode, to_parent_vnode, name, namelen);
                vput(from_vnode);
                vput(to_parent_vnode);
                return retval;
        // }

        // panic("Should not react here!!! Below code is added to avoid getting a compiler warning!!!\n");
        // return 0; 
        
}

/*      o link newname to oldname
 *      o unlink oldname
 *      o return the value of unlink, or an error
 *
 * Note that this does not provide the same behavior as the
 * Linux system call (if unlink fails then two links to the
 * file could exist).
 */
int
do_rename(const char *oldname, const char *newname)
{
        int retval = do_link(oldname, newname);
        // if(retval < 0)
        // {
                return retval;
        // }
        
        // panic("Should not react here!!! Below code is added to avoid getting a compiler warning!!!\n");
        // return 0; 
        // To fix warnings
        // return do_unlink(oldname);
}

/* Make the named directory the current process's cwd (current working
 * directory).  Don't forget to down the refcount to the old cwd (vput()) and
 * up the refcount to the new cwd (open_namev() or vget()). Return 0 on
 * success.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o ENOENT
 *        path does not exist.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 *      o ENOTDIR
 *        A component of path is not a directory.
 */
int
do_chdir(const char *path)
{
        int retval;
        size_t namelen = 0;
        const char *name = NULL;
        vnode_t *res_vnode = NULL;
        struct vnode *base = NULL;

        // getting the parent vnode at res_vnode below
        retval = dir_namev(path, &namelen, &name, base, &res_vnode);
        // if(retval < 0){
        //         return retval;
        // }

        vnode_t *lookup_result = NULL;
        retval = lookup(res_vnode,name,namelen,&lookup_result);

        if(retval < 0)
        {
                vput(res_vnode);
                return retval;
        }

        if(!S_ISDIR(lookup_result->vn_mode)) {
                vput(lookup_result);
                vput(res_vnode);
                return -ENOTDIR;
        }
        
        vput(res_vnode);
        vput(curproc->p_cwd);
        curproc->p_cwd = lookup_result;
        return 0;
}

/* Call the readdir vn_op on the given fd, filling in the given dirent_t*.
 * If the readdir vn_op is successful, it will return a positive value which
 * is the number of bytes copied to the dirent_t.  You need to increment the
 * file_t's f_pos by this amount.  As always, be aware of refcounts, check
 * the return value of the fget and the virtual function, and be sure the
 * virtual function exists (is not null) before calling it.
 *
 * Return either 0 or sizeof(dirent_t), or -errno.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        Invalid file descriptor fd.
 *      o ENOTDIR
 *        File descriptor does not refer to a directory.
 */
int
do_getdent(int fd, struct dirent *dirp)
{

        // checking if the fd exists for the current process and then if that function exists and then get the data in that structure
        file_t *file = fget(fd); // fref(f) if fd exists
        if(file == NULL)
        {
                // fd isn't an open file descriptor.
                return -EBADF;
        }
        if(file->f_mode == 0)
        {
                fput(file);
                return -EBADF;
        }

        // curproc->p_files[fd];
        // readdir only allowed in directories

        /*
         * readdir reads one directory entry from the dir into the struct
         * dirent. On success, it returns the amount that offset should be
         * increased by to obtain the next directory entry with a
         * subsequent call to readdir. If the end of the file as been
         * reached (offset == file->vn_len), no directory entry will be
         * read and 0 will be returned.
         */
        // int (*readdir)(struct vnode *dir, off_t offset, struct dirent *d);

        if(file->f_vnode->vn_ops->readdir == NULL)
        {
                fput(file);
                return -ENOTDIR;
        }
        
        int retval = file->f_vnode->vn_ops->readdir(file->f_vnode, file->f_pos, dirp);
        
        if(retval != 0)
        {
                // If the readdir vn_op is successful, it will return a positive value which
                // * is the number of bytes copied to the dirent_t.  You need to increment the
                //  * file_t's f_pos by this amount
                file->f_pos += retval;
        }
        else{
                fput(file);
                return 0;
        }

        
        fput(file);
        return sizeof(dirent_t);
}

/*
 * Modify f_pos according to offset and whence.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not an open file descriptor.
 *      o EINVAL
 *        whence is not one of SEEK_SET, SEEK_CUR, SEEK_END; or the resulting
 *        file offset would be negative.
 */
int
do_lseek(int fd, int offset, int whence)
{
        //todo: check which are 2A
        if(whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END){
                return -EINVAL;
        }
        file_t *op_file = fget(fd);
        if(op_file){
                // invalid file
                if(op_file->f_mode == 0)
                {
                        fput(op_file);
                        return -EBADF;
                }
                if(whence == SEEK_SET){
                        if (offset >= 0) {
                                op_file->f_pos = offset;
                        } else {
                                fput(op_file);
                                return -EINVAL;
                        }
                }
                else if(whence == SEEK_CUR){
                        if(op_file->f_pos + offset >= 0){
                                op_file->f_pos += offset;
                        }
                        else{
                                fput(op_file);
                                return -EINVAL;
                        }
                }
                if(whence == SEEK_END){
                        if(op_file->f_vnode->vn_len + offset >= 0){
                                op_file->f_pos = op_file->f_vnode->vn_len + offset;
                        }
                        else{
                                fput(op_file);
                                return -EINVAL;
                        }
                }
                fput(op_file);
                return op_file->f_pos;
        }
        else{
                return -EBADF;
        }

        panic("Should never get here!\n");
}

/*
 * Find the vnode associated with the path, and call the stat() vnode operation.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o ENOENT
 *        A component of path does not exist.
 *      o ENOTDIR
 *        A component of the path prefix of path is not a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 *      o EINVAL
 *        path is an empty string.
 */
int
do_stat(const char *path, struct stat *buf)
{
        if (path == NULL || path[0] == '\0') {
                return -EINVAL;
        }

        int retval;
        const char *name = NULL;
        size_t namelen = 0;
        vnode_t *parent_directory = NULL;
        vnode_t *base = NULL;

        retval = dir_namev(path,&namelen,&name,base,&parent_directory);
        if(retval < 0) {
                return retval;
        }

        if(namelen == 0){
                name = ".";
                namelen = 1;
        }

        vnode_t *file_vnode;
        retval = lookup(parent_directory,name,namelen,&file_vnode);
        if(retval == -ENOTDIR || retval == -ENOENT) {
                vput(parent_directory);
                return retval;
        }

        KASSERT(NULL != file_vnode->vn_ops->stat);

        file_vnode->vn_ops->stat(file_vnode,buf);
        if (parent_directory) {
                vput(parent_directory);
        }
        if (file_vnode) {
                vput(file_vnode);
        }

        return 0;
}

#ifdef __MOUNTING__
/*
 * Implementing this function is not required and strongly discouraged unless
 * you are absolutely sure your Weenix is perfect.
 *
 * This is the syscall entry point into vfs for mounting. You will need to
 * create the fs_t struct and populate its fs_dev and fs_type fields before
 * calling vfs's mountfunc(). mountfunc() will use the fields you populated
 * in order to determine which underlying filesystem's mount function should
 * be run, then it will finish setting up the fs_t struct. At this point you
 * have a fully functioning file system, however it is not mounted on the
 * virtual file system, you will need to call vfs_mount to do this.
 *
 * There are lots of things which can go wrong here. Make sure you have good
 * error handling. Remember the fs_dev and fs_type buffers have limited size
 * so you should not write arbitrary length strings to them.
 */
int
do_mount(const char *source, const char *target, const char *type)
{
        NOT_YET_IMPLEMENTED("MOUNTING: do_mount");
        return -EINVAL;
}

/*
 * Implementing this function is not required and strongly discouraged unless
 * you are absolutley sure your Weenix is perfect.
 *
 * This function delegates all of the real work to vfs_umount. You should not worry
 * about freeing the fs_t struct here, that is done in vfs_umount. All this function
 * does is figure out which file system to pass to vfs_umount and do good error
 * checking.
 */
int
do_umount(const char *target)
{
        NOT_YET_IMPLEMENTED("MOUNTING: do_umount");
        return -EINVAL;
}
#endif
