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

#include "kernel.h"
#include "globals.h"
#include "types.h"
#include "errno.h"

#include "util/string.h"
#include "util/printf.h"
#include "util/debug.h"

#include "fs/dirent.h"
#include "fs/fcntl.h"
#include "fs/stat.h"
#include "fs/vfs.h"
#include "fs/vnode.h"

#include "mm/kmalloc.h"

/* This takes a base 'dir', a 'name', its 'len', and a result vnode.
 * Most of the work should be done by the vnode's implementation
 * specific lookup() function.
 *
 * If dir has no lookup(), return -ENOTDIR.
 *
 * Note: returns with the vnode refcount on *result incremented.
 */
int
lookup(vnode_t *dir, const char *name, size_t len, vnode_t **result)
{       
        KASSERT(NULL != dir); /* the "dir" argument must be non-NULL */       
        KASSERT(NULL != name); /* the "name" argument must be non-NULL */    
        KASSERT(NULL != result); /* the "result" argument must be non-NULL */

        if(dir->vn_ops == NULL || dir->vn_ops->lookup == NULL) {
                return -ENOTDIR;
        }

        return dir->vn_ops->lookup(dir, name, len, result);
}


/* When successful this function returns data in the following "out"-arguments:
 *  o res_vnode: the vnode of the parent directory of "name"
 *  o name: the `basename' (the element of the pathname)
 *  o namelen: the length of the basename
 *
 * For example: dir_namev("/s5fs/bin/ls", &namelen, &name, NULL,
 * &res_vnode) would put 2 in namelen, "ls" in name, and a pointer to the
 * vnode corresponding to "/s5fs/bin" in res_vnode.
 *
 * The "base" argument defines where we start resolving the path from:
 * A base value of NULL means to use the process's current working directory,
 * curproc->p_cwd.  If pathname[0] == '/', ignore base and start with
 * vfs_root_vn.  dir_namev() should call lookup() to take care of resolving each
 * piece of the pathname.
 *
 * Note: A successful call to this causes vnode refcount on *res_vnode to
 * be incremented.
 */
int
dir_namev(const char *pathname, size_t *namelen, const char **name,
          vnode_t *base, vnode_t **res_vnode)
{

    KASSERT(NULL != pathname);
    KASSERT(NULL != namelen);
    KASSERT(NULL != name);
    KASSERT(NULL != res_vnode);

    size_t len = strlen(pathname);
    char *path_to_resolve = (char *)kmalloc(len + 1);
    char *path_ptr = path_to_resolve;
    strcpy(path_to_resolve, pathname);

    vnode_t *curr_base, *result, *prev = NULL;

    if (path_to_resolve[0] == '/')
    {
        curr_base = vfs_root_vn;
        path_to_resolve = path_to_resolve + 1;
    } 
    else {
        if (base != NULL) {
            curr_base = base;
        }
        else {
            curr_base = curproc->p_cwd;
        }
    }

    KASSERT(NULL != curr_base);
    
    int i;
    char **components = kmalloc(50 * sizeof(char *));
    int component_idx = 0;

    components[component_idx] = &path_to_resolve[0];
    for (i = 1; path_to_resolve[i] != 0; i++)
    {
        if (path_to_resolve[i] == '/')
        {
            component_idx++;
            components[component_idx] = &path_to_resolve[i + 1];
            path_to_resolve[i] = '\0';
        }
    }

    i = 0;
    if (component_idx == 0) {
        *name = strstr(pathname, components[0]);
        *namelen = strlen(components[0]);
        *res_vnode = curr_base;
        
        vref(curr_base);
        kfree(components);
        kfree(path_ptr);
        
        return 0;
    }

    for (i = 0; i < component_idx; i++) {

        char *component = components[i];
        if (strlen(component) == 0) {
            continue;
        }

        int ret;
        if ((ret = lookup(curr_base, component, strlen(component), &result)) < 0) {
            if (prev != NULL) {
                vput(prev);
            }

            kfree(components);
            kfree(path_ptr);
            return ret;
        }

        if (prev != NULL) {
            vput(prev);
        }

        if (!S_ISDIR(result->vn_mode)) {
            vput(result);
            
            kfree(components);
            kfree(path_ptr);
            return -ENOTDIR;
        }

        curr_base = result;
        prev = result;
    }

    *name = strstr(pathname, components[i]);
    *namelen = strlen(components[i]);
    *res_vnode = curr_base;

    kfree(components);
    kfree(path_ptr);
    
    return 0;
    
}

/* This returns in res_vnode the vnode requested by the other parameters.
 * It makes use of dir_namev and lookup to find the specified vnode (if it
 * exists).  flag is right out of the parameters to open(2); see
 * <weenix/fcntl.h>.  If the O_CREAT flag is specified and the file does
 * not exist, call create() in the parent directory vnode. However, if the
 * parent directory itself does not exist, this function should fail - in all
 * cases, no files or directories other than the one at the very end of the path
 * should be created.
 *
 * Note: Increments vnode refcount on *res_vnode.
 */
int
open_namev(const char *pathname, int flag, vnode_t **res_vnode, vnode_t *base)
{
    int retval;
    vnode_t *dir_vnode = NULL;
    size_t file_len = 0;
    const char *file_name = NULL;
    
    retval = dir_namev(pathname, &file_len, &file_name, base, &dir_vnode);
    if (retval != 0) {
        return retval;
    }
    
    if (strncmp(file_name, "", file_len) == 0) {  // Handling case where file_name is empty
        *res_vnode = dir_vnode;
        return 0;
    }
    
    retval = lookup(dir_vnode, file_name, file_len, res_vnode);
    if (retval != 0) {
        if ((flag & O_CREAT) && retval == -ENOENT) {  // Check if the file needs to be created
            KASSERT(NULL != dir_vnode->vn_ops->create);  // Assert the create operation is supported
            retval = dir_vnode->vn_ops->create(dir_vnode, file_name, file_len, res_vnode);
            vput(dir_vnode);  // Release the directory vnode after use
            if (retval != 0) {
                *res_vnode = NULL;  // Ensure res_vnode is NULL on error
                return retval;
            }
            return 0;
        }
        vput(dir_vnode);  // Release the directory vnode if lookup failed and not creating
        return retval;
    }
    
    if (file_name[file_len] == '/') {  // Additional handling if pathname is supposed to be a directory
        if (!S_ISDIR((*res_vnode)->vn_mode)) {
            vput(dir_vnode);
            vput(*res_vnode);
            return -ENOTDIR;
        }
    }
    
    vput(dir_vnode);  // Release the directory vnode as its job is done after a successful lookup
    return 0;
}

#ifdef __GETCWD__
/* Finds the name of 'entry' in the directory 'dir'. The name is writen
 * to the given buffer. On success 0 is returned. If 'dir' does not
 * contain 'entry' then -ENOENT is returned. If the given buffer cannot
 * hold the result then it is filled with as many characters as possible
 * and a null terminator, -ERANGE is returned.
 *
 * Files can be uniquely identified within a file system by their
 * inode numbers. */
int
lookup_name(vnode_t *dir, vnode_t *entry, char *buf, size_t size)
{
        NOT_YET_IMPLEMENTED("GETCWD: lookup_name");
        return -ENOENT;
}


/* Used to find the absolute path of the directory 'dir'. Since
 * directories cannot have more than one link there is always
 * a unique solution. The path is writen to the given buffer.
 * On success 0 is returned. On error this function returns a
 * negative error code. See the man page for getcwd(3) for
 * possible errors. Even if an error code is returned the buffer
 * will be filled with a valid string which has some partial
 * information about the wanted path. */
ssize_t
lookup_dirpath(vnode_t *dir, char *buf, size_t osize)
{
        NOT_YET_IMPLEMENTED("GETCWD: lookup_dirpath");
        return -ENOENT;
}
#endif /* __GETCWD__ */
