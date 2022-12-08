#ifndef _new_fs_H_
#define _new_fs_H_

#define FUSE_USE_VERSION 26
#include "stdio.h"
#include "stdlib.h"
#include <unistd.h>
#include "fcntl.h"
#include "string.h"
#include "fuse.h"
#include <stddef.h>
#include "ddriver.h"
#include "errno.h"
#include "types.h"


/******************************************************************************
* SECTION: global region
*******************************************************************************/
// struct new_fs_super      new_fs_super; 
// struct custom_options new_fs_options;
/******************************************************************************
* SECTION: macro debug
*******************************************************************************/
#define new_fs_DBG(fmt, ...) do { printf("new_fs_DBG: " fmt, ##__VA_ARGS__); } while(0) 
/******************************************************************************
* SECTION: new_fs_utils.c
*******************************************************************************/
char* 			   new_fs_get_fname(const char* path);
int 			   new_fs_calc_lvl(const char * path);
int 			   new_fs_driver_read(int offset, uint8_t *out_content, int size);
int 			   new_fs_driver_write(int offset, uint8_t *in_content, int size);
int 			   new_fs_alloc_dentry(struct new_fs_inode* inode, struct new_fs_dentry* dentry);
struct new_fs_inode*  new_fs_alloc_inode(struct new_fs_dentry * dentry);
int 			   new_fs_sync_inode(struct new_fs_inode * inode);
struct new_fs_inode*  new_fs_read_inode(struct new_fs_dentry * dentry, int ino);

struct new_fs_dentry* new_fs_get_dentry(struct new_fs_inode * inode, int dir);
struct new_fs_dentry* new_fs_lookup(const char * path, boolean* is_find, boolean* is_root);

int 			   new_fs_mount(struct custom_options options);
int 			   new_fs_umount();

/******************************************************************************
* SECTION: new_fs.c
*******************************************************************************/
void* 			   new_fs_init(struct fuse_conn_info *);
void  			   new_fs_destroy(void *);
int   			   new_fs_mkdir(const char *, mode_t);
int   			   new_fs_getattr(const char *, struct stat *);
int   			   new_fs_readdir(const char *, void *, fuse_fill_dir_t, off_t,
						                struct fuse_file_info *);
int   			   new_fs_mknod(const char *, mode_t, dev_t);
int   			   new_fs_write(const char *, const char *, size_t, off_t,
					                  struct fuse_file_info *);
int   			   new_fs_read(const char *, char *, size_t, off_t,
					                 struct fuse_file_info *);
int   			   new_fs_access(const char *, int);
int   			   new_fs_unlink(const char *);
int   			   new_fs_rmdir(const char *);
int   			   new_fs_rename(const char *, const char *);
int   			   new_fs_utimens(const char *, const struct timespec tv[2]);
int   			   new_fs_truncate(const char *, off_t);
			
int   			   new_fs_open(const char *, struct fuse_file_info *);
int   			   new_fs_opendir(const char *, struct fuse_file_info *);

#endif  /* _new_fs_H_ */