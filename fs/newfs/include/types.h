#ifndef _TYPES_H_
#define _TYPES_H_

/******************************************************************************
* SECTION: Type def
*******************************************************************************/
typedef int          boolean;
typedef uint16_t     flag16;

typedef enum new_fs_file_type {
    new_fs_REG_FILE,
    new_fs_DIR
} new_fs_FILE_TYPE;
/******************************************************************************
* SECTION: Macro
*******************************************************************************/
#define TRUE                    1
#define FALSE                   0
#define UINT32_BITS             32
#define UINT8_BITS              8

#define new_fs_MAGIC_NUM           0x20011115  /* Define by yourself */
#define new_fs_SUPER_OFS           0
#define new_fs_ROOT_INO            0

/* 规定各区间占的 BLK 数 */
#define new_fs_SUPER_BLKS          1
#define new_fs_MAP_INODE_BLKS      1
#define new_fs_MAP_DATA_BLKS       1
#define new_fs_INODE_BLKS          512
#define new_fs_DATA_BLKS           2048

#define new_fs_ERROR_NONE          0
#define new_fs_ERROR_ACCESS        EACCES
#define new_fs_ERROR_SEEK          ESPIPE     
#define new_fs_ERROR_ISDIR         EISDIR
#define new_fs_ERROR_NOSPACE       ENOSPC
#define new_fs_ERROR_EXISTS        EEXIST
#define new_fs_ERROR_NOTFOUND      ENOENT
#define new_fs_ERROR_UNSUPPORTED   ENXIO
#define new_fs_ERROR_IO            EIO     /* Error Input/Output */
#define new_fs_ERROR_INVAL         EINVAL  /* Invalid Args */

#define new_fs_MAX_FILE_NAME       128
#define new_fs_DATA_PER_FILE       6
#define new_fs_DEFAULT_PERM        0777     /* 全权限打开 */

#define new_fs_IOC_MAGIC           'S'
#define new_fs_IOC_SEEK            _IO(new_fs_IOC_MAGIC, 0)

#define new_fs_FLAG_BUF_DIRTY      0x1
#define new_fs_FLAG_BUF_OCCUPY     0x2

/******************************************************************************
* SECTION: Macro Function
*******************************************************************************/
#define new_fs_IO_SZ()                     (new_fs_super.sz_io)       /*inode的大小512B*/
#define new_fs_BLK_SZ()                    (new_fs_super.sz_blk)      /*EXT2文件系统一个块大小1024B*/
#define new_fs_DISK_SZ()                   (new_fs_super.sz_disk)     /*磁盘大小4MB*/
#define new_fs_DRIVER()                    (new_fs_super.driver_fd)

#define new_fs_ROUND_DOWN(value, round)    ((value) % (round) == 0 ? (value) : ((value) / (round)) * (round))
#define new_fs_ROUND_UP(value, round)      ((value) % (round) == 0 ? (value) : ((value) / (round) + 1) * (round))

#define new_fs_BLKS_SZ(blks)               ((blks) * new_fs_BLK_SZ())
#define new_fs_ASSIGN_FNAME(pnew_fs_dentry, _fname) memcpy(pnew_fs_dentry->fname, _fname, strlen(_fname))
#define new_fs_INO_OFS(ino)                (new_fs_super.inode_offset + (ino) * new_fs_BLK_SZ())
#define new_fs_DATA_OFS(bno)               (new_fs_super.data_offset + (bno) * new_fs_BLK_SZ())

#define new_fs_IS_DIR(pinode)              (pinode->dentry->ftype == new_fs_DIR)
#define new_fs_IS_REG(pinode)              (pinode->dentry->ftype == new_fs_REG_FILE)
/******************************************************************************
* SECTION: FS Specific Structure - In memory structure 内存架构
*******************************************************************************/
struct new_fs_super;
struct new_fs_dentry;
struct new_fs_inode;


struct custom_options {
	const char*        device;
    boolean            show_help;
};

struct new_fs_super {
    int                 driver_fd;          /* 打开的磁盘句柄 */

    int                 sz_io;              /* 512B */
    int                 sz_blk;             /* 1024B */
    int                 sz_disk;            /* 4MB */
    int                 sz_usage;           /* ioctl 相关信息 */

    int                 max_ino;            /*inode的数目，即最多支持的文件数*/
    int                 max_data;           /*data索引数据块的数目*/

    uint8_t*            map_inode;          /* 指向 inode 位图的内存起点(数组) */ 
    int                 map_inode_blks;     /* inode 位图占用的块数 */
    int                 map_inode_offset;   /* inode 位图在磁盘上的偏移(在数组起点基础上) */

    uint8_t*            map_data;           /* 指向 data 位图的内存起点(数组) */ 
    int                 map_data_blks;      /* data 位图占用的块数 */
    int                 map_data_offset;    /* data 位图在磁盘上的偏移（在数组起点的基础上） */

    int                 inode_offset;       /* 索引结点的偏移 */
    int                 data_offset;        /* 数据块的偏移*/

    boolean             is_mounted;

    struct new_fs_dentry*  root_dentry;
};

struct new_fs_inode {
    uint32_t            ino;                                 /* 在inode位图中的下标 */
    int                 size;                               /* 文件已占用空间 */
    int                 dir_cnt;                             /* 如果是目录类型文件，下面有几个目录项 */
    struct new_fs_dentry*  dentry;                             /* 指向该 inode 的 父dentry */
    struct new_fs_dentry*  dentrys;                            /* 如果是 DIR，指向其所有子项（用链表串接）*/
    uint8_t*            block_pointer[new_fs_DATA_PER_FILE];   /* 如果是 FILE，指向 4 个数据块，四倍结构 */
    int                 datano[new_fs_DATA_PER_FILE];             /* 数据块在磁盘中的块号 */
};

struct new_fs_dentry {
    char                fname[new_fs_MAX_FILE_NAME];
    struct new_fs_dentry*  parent;             /* 父亲 Inode 的 dentry */
    struct new_fs_dentry*  brother;            /* 下一个兄弟 Inode 的 dentry */
    uint32_t            ino;                    /* 指向的ino号 */
    struct new_fs_inode*   inode;              /* 指向inode */
    new_fs_FILE_TYPE       ftype;               /* 文件类型 */
    int     valid;                                    /* 该目录项是否有效 */ 
};


static inline struct new_fs_dentry* new_dentry(char * fname, new_fs_FILE_TYPE ftype) {
    struct new_fs_dentry * dentry = (struct new_fs_dentry *)malloc(sizeof(struct new_fs_dentry)); /* dentry 在内存空间也是随机分配 */
    memset(dentry, 0, sizeof(struct new_fs_dentry));
    new_fs_ASSIGN_FNAME(dentry, fname);
    dentry->ftype   = ftype;
    dentry->ino     = -1;
    dentry->inode   = NULL;
    dentry->parent  = NULL;
    dentry->brother = NULL;
    return dentry;                                            
}

/******************************************************************************
* SECTION: FS Specific Structure - Disk structure
*******************************************************************************/
struct new_fs_super_d {
    uint32_t    magic_num;
    int         sz_usage;

    int         map_inode_blks;     /* inode 位图占用的块数 */
    int         map_inode_offset;   /* inode 位图在磁盘上的偏移 */

    int         map_data_blks;      /* data 位图占用的块数 */
    int         map_data_offset;    /* data 位图在磁盘上的偏移 */

    int         inode_offset;       /* 索引结点的偏移 */
    int         data_offset;        /* 数据块的偏移*/
};

struct new_fs_inode_d {
    uint32_t        ino;            /* 在inode位图中的下标 */
    int             size;           /* 文件已占用空间 */
    int             dir_cnt;        /* 如果是目录类型文件，下面有几个目录项 */
    new_fs_FILE_TYPE   ftype;        /* 文件类型 */
    int             datano[new_fs_DATA_PER_FILE];       /* 数据块在磁盘中的块号 */
};

struct new_fs_dentry_d {
    char            fname[new_fs_MAX_FILE_NAME];
    new_fs_FILE_TYPE   ftype;
    uint32_t        ino;            /* 指向的 ino 号 */
    int     valid;    /* 该目录项是否有效 */  
};

#endif /* _TYPES_H_ */