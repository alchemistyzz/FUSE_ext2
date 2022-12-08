#include "new_fs.h"

/******************************************************************************
* SECTION: 宏定义
*******************************************************************************/
#define OPTION(t, p)        { t, offsetof(struct custom_options, p), 1 }

/******************************************************************************
* SECTION: 全局变量
*******************************************************************************/
static const struct fuse_opt option_spec[] = {		/* 用于FUSE文件系统解析参数 */
	OPTION("--device=%s", device),
	FUSE_OPT_END
};

struct custom_options new_fs_options;			/* 全局选项 */
struct new_fs_super new_fs_super; 					/* 内存中的超级块 */
/******************************************************************************
* SECTION: FUSE操作定义
*******************************************************************************/
static struct fuse_operations operations = {
	.init = new_fs_init,						/* mount文件系统 */		
	.destroy = new_fs_destroy,				 	/* umount文件系统 */
	.mkdir = new_fs_mkdir,					 	/* 建目录，mkdir */
	.getattr = new_fs_getattr,				 	/* 获取文件属性，类似stat，必须完成 */
	.readdir = new_fs_readdir,				 	/* 填充dentrys */
	.mknod = new_fs_mknod,					 	/* 创建文件，touch相关 */
	.write = NULL,							/* 写入文件 */
	.read = NULL,							/* 读文件 */
	.utimens = new_fs_utimens,				 	/* 修改时间，忽略，避免touch报错 */
	.truncate = NULL,						/* 改变文件大小 */
	.unlink = NULL,							/* 删除文件 */
	.rmdir	= NULL,							/* 删除目录， rm -r */
	.rename = NULL,							/* 重命名，mv */

	.open = NULL,							
	.opendir = NULL,
	.access = NULL
};
/******************************************************************************
* SECTION: 必做函数实现
*******************************************************************************/
/**
 * @brief 挂载（mount）文件系统
 * 
 * @param conn_info 可忽略，一些建立连接相关的信息 
 * @return void*
 */
void* new_fs_init(struct fuse_conn_info * conn_info) {
	/* TODO: 在这里进行挂载 */
	if (new_fs_mount(new_fs_options) != new_fs_ERROR_NONE) {
        new_fs_DBG("[%s] mount error\n", __func__);
		fuse_exit(fuse_get_context()->fuse);
		return NULL;
	} 
	return NULL;
}

/**
 * @brief 卸载（umount）文件系统
 * 
 * @param p 可忽略
 * @return void
 */
void new_fs_destroy(void* p) {
	/* TODO: 在这里进行卸载 */
	if (new_fs_umount() != new_fs_ERROR_NONE) {
		new_fs_DBG("[%s] unmount error\n", __func__);
		fuse_exit(fuse_get_context()->fuse);
		return;
	}
	return;
}

/**
 * @brief 创建目录
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建模式（只读？只写？），可忽略
 * @return int 0成功，否则失败
 */
int new_fs_mkdir(const char* path, mode_t mode) {
	/* TODO: 解析路径，创建目录 */
	(void)mode;
	boolean is_find, is_root;
	char* fname;
	struct new_fs_dentry* last_dentry = new_fs_lookup(path, &is_find, &is_root);
	struct new_fs_dentry* dentry;
	struct new_fs_inode*  inode;
	/*如果目录存在则返回错误*/
	if (is_find) {
		return -new_fs_ERROR_EXISTS;
	}

	/*若上级目录为文件类型也返回错误*/
	if (new_fs_IS_REG(last_dentry->inode)) {
		return -new_fs_ERROR_UNSUPPORTED;
	}
	/*创建目录并建立连接,并创建对应inode节点*/
	fname  = new_fs_get_fname(path);
	dentry = new_dentry(fname, new_fs_DIR); 
	dentry->parent = last_dentry;
	inode  = new_fs_alloc_inode(dentry);
	new_fs_alloc_dentry(last_dentry->inode, dentry);
	
	return new_fs_ERROR_NONE;
}

/**
 * @brief 获取文件或目录的属性，该函数非常重要
 * 主要思路是先解析路径，然后从中获取相应的文件的inode或者是dentry，最后填写new_fs_stat的结构体
 * @param path 相对于挂载点的路径
 * @param new_fs_stat 返回状态
 * @return int 0成功，否则失败
 */
int new_fs_getattr(const char* path, struct stat * new_fs_stat) {
	boolean	is_find, is_root;
	/*首先找到路径对应的目录项*/
	struct new_fs_dentry* dentry = new_fs_lookup(path, &is_find, &is_root);
	/*若根据目录无法找到则报错*/
	
	if (is_find == FALSE) {
		return -new_fs_ERROR_NOTFOUND;
	}
	/*判断目录项的文件类型并对状态进行编写*/
	if (new_fs_IS_DIR(dentry->inode)) {//如果是目录
		new_fs_stat->st_mode = S_IFDIR | new_fs_DEFAULT_PERM;
		new_fs_stat->st_size = dentry->inode->dir_cnt * sizeof(struct new_fs_dentry_d);
	}
	else if (new_fs_IS_REG(dentry->inode)) {
		new_fs_stat->st_mode = S_IFREG | new_fs_DEFAULT_PERM;
		new_fs_stat->st_size = dentry->inode->size;
	}

	new_fs_stat->st_nlink = 1;
	new_fs_stat->st_uid 	 = getuid();
	new_fs_stat->st_gid 	 = getgid();
	new_fs_stat->st_atime   = time(NULL);
	new_fs_stat->st_mtime   = time(NULL);
	new_fs_stat->st_blksize = new_fs_BLK_SZ(); /* 这里修改了 */

	if (is_root) {
		new_fs_stat->st_size	= new_fs_super.sz_usage; 
		new_fs_stat->st_blocks = new_fs_DISK_SZ() / new_fs_BLK_SZ(); /* 这里修改了 */
		new_fs_stat->st_nlink  = 2;		/* !特殊，根目录link数为2 */
	}
	return new_fs_ERROR_NONE;
}

/**
 * @brief 遍历目录项，填充至buf，并交给FUSE输出
 * 
 * @param path 相对于挂载点的路径
 * @param buf 输出buffer
 * @param filler 参数讲解:
 * 
 * typedef int (*fuse_fill_dir_t) (void *buf, const char *name,
 *				const struct stat *stbuf, off_t off)
 * buf: name会被复制到buf中
 * name: dentry名字
 * stbuf: 文件状态，可忽略
 * off: 下一次offset从哪里开始，这里可以理解为第几个dentry
 * 
 * @param offset 第几个目录项？
 * @param fi 可忽略
 * @return int 0成功，否则失败
 */
int new_fs_readdir(const char * path, void * buf, fuse_fill_dir_t filler, off_t offset,
			    		 struct fuse_file_info * fi) {
    /* TODO: 解析路径，获取目录的Inode，并读取目录项，利用filler填充到buf，可参考/fs/simplefs/new_fs.c的new_fs_readdir()函数实现 */
    boolean	is_find, is_root;
	int		cur_dir = offset;

	struct new_fs_dentry* dentry = new_fs_lookup(path, &is_find, &is_root);
	struct new_fs_dentry* sub_dentry;
	struct new_fs_inode* inode;
	if (is_find) {
		inode = dentry->inode;
		sub_dentry = new_fs_get_dentry(inode, cur_dir);
		if (sub_dentry) {
			filler(buf, sub_dentry->fname, NULL, ++offset);
		}
		return new_fs_ERROR_NONE;
	}
	return -new_fs_ERROR_NOTFOUND;
}

/**
 * @brief 创建文件
 * 主要思路：寻找上级目录，然后创建目录同时建立连接
 * @param path 相对于挂载点的路径
 * @param mode 创建文件的模式，可忽略
 * @param dev 设备类型，可忽略
 * @return int 0成功，否则失败
 */
int new_fs_mknod(const char* path, mode_t mode, dev_t dev) {
	boolean	is_find, is_root;
	
	struct new_fs_dentry* last_dentry = new_fs_lookup(path, &is_find, &is_root);
	struct new_fs_dentry* dentry;
	struct new_fs_inode* inode;
	char* fname;
	
	if (is_find == TRUE) {//目录存在，报错返回
		return -new_fs_ERROR_EXISTS;
	}

	fname = new_fs_get_fname(path);
	
	if (S_ISREG(mode)) {//如果是文件
		dentry = new_dentry(fname, new_fs_REG_FILE);
	}
	else if (S_ISDIR(mode)) {//如果是目录
		dentry = new_dentry(fname, new_fs_DIR);
	}
	dentry->parent = last_dentry;
	inode = new_fs_alloc_inode(dentry);
	new_fs_alloc_dentry(last_dentry->inode, dentry);

	return new_fs_ERROR_NONE;
}

/**
 * @brief 修改时间，为了不让touch报错 
 * 
 * @param path 相对于挂载点的路径
 * @param tv 实践
 * @return int 0成功，否则失败
 */
int new_fs_utimens(const char* path, const struct timespec tv[2]) {
	(void)path;
	return new_fs_ERROR_NONE;
}
/******************************************************************************
* SECTION: 选做函数实现
*******************************************************************************/
/**
 * @brief 写入文件
 * 
 * @param path 相对于挂载点的路径
 * @param buf 写入的内容
 * @param size 写入的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 写入大小
 */
int new_fs_write(const char* path, const char* buf, size_t size, off_t offset,
		        struct fuse_file_info* fi) {
	/* 选做 */
	return size;
}

/**
 * @brief 读取文件
 * 
 * @param path 相对于挂载点的路径
 * @param buf 读取的内容
 * @param size 读取的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 读取大小
 */
int new_fs_read(const char* path, char* buf, size_t size, off_t offset,
		       struct fuse_file_info* fi) {
	/* 选做 */
	return size;			   
}

/**
 * @brief 删除文件
 * 
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则失败
 */
int new_fs_unlink(const char* path) {
	/* 选做 */
	return 0;
}

/**
 * @brief 删除目录
 * 
 * 一个可能的删除目录操作如下：
 * rm ./tests/mnt/j/ -r
 *  1) Step 1. rm ./tests/mnt/j/j
 *  2) Step 2. rm ./tests/mnt/j
 * 即，先删除最深层的文件，再删除目录文件本身
 * 
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则失败
 */
int new_fs_rmdir(const char* path) {
	/* 选做 */
	return 0;
}

/**
 * @brief 重命名文件 
 * 
 * @param from 源文件路径
 * @param to 目标文件路径
 * @return int 0成功，否则失败
 */
int new_fs_rename(const char* from, const char* to) {
	/* 选做 */
	return 0;
}

/**
 * @brief 打开文件，可以在这里维护fi的信息，例如，fi->fh可以理解为一个64位指针，可以把自己想保存的数据结构
 * 保存在fh中
 * 
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则失败
 */
int new_fs_open(const char* path, struct fuse_file_info* fi) {
	/* 选做 */
	return 0;
}

/**
 * @brief 打开目录文件
 * 
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则失败
 */
int new_fs_opendir(const char* path, struct fuse_file_info* fi) {
	/* 选做 */
	return 0;
}

/**
 * @brief 改变文件大小
 * 
 * @param path 相对于挂载点的路径
 * @param offset 改变后文件大小
 * @return int 0成功，否则失败
 */
int new_fs_truncate(const char* path, off_t offset) {
	/* 选做 */
	return 0;
}


/**
 * @brief 访问文件，因为读写文件时需要查看权限
 * 
 * @param path 相对于挂载点的路径
 * @param type 访问类别
 * R_OK: Test for read permission. 
 * W_OK: Test for write permission.
 * X_OK: Test for execute permission.
 * F_OK: Test for existence. 
 * 
 * @return int 0成功，否则失败
 */
int new_fs_access(const char* path, int type) {
	/* 选做: 解析路径，判断是否存在 */
	return 0;
}	
/******************************************************************************
* SECTION: FUSE入口
*******************************************************************************/
int main(int argc, char **argv)
{
    int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	new_fs_options.device = strdup("/home/students/200111102/ddriver");

	if (fuse_opt_parse(&args, &new_fs_options, option_spec, NULL) == -1)
		return -1;
	
	ret = fuse_main(args.argc, args.argv, &operations, NULL);
	fuse_opt_free_args(&args);
	return ret;
}