#include "../include/new_fs.h"

extern struct new_fs_super      new_fs_super; 
extern struct custom_options   new_fs_options;
/**jg
 * @brief 获取文件名
 * 
 * @param path 
 * @return char* 
 */
char* new_fs_get_fname(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}
/**
 * @brief 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 * @param path 
 * @return int 
 */
int new_fs_calc_lvl(const char * path) {
    // char* path_cpy = (char *)malloc(strlen(path));
    // strcpy(path_cpy, path);
    char* str = path;
    int   lvl = 0;
    if (strcmp(path, "/") == 0) {
        return lvl;
    }
    while (*str != NULL) {
        if (*str == '/') {
            lvl++;
        }
        str++;
    }
    return lvl;
}
/**
 * @brief 驱动读
 * 
 * @param offset 
 * @param out_content 
 * @param size 
 * @return int 
 */
int new_fs_driver_read(int offset, uint8_t *out_content, int size) {
    /* 按 1024B 读取*/
    int      offset_aligned = new_fs_ROUND_DOWN(offset, new_fs_BLK_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = new_fs_ROUND_UP((size + bias), new_fs_BLK_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    // lseek(new_fs_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(new_fs_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // read(new_fs_DRIVER(), cur, new_fs_IO_SZ());
        ddriver_read(new_fs_DRIVER(), cur, new_fs_IO_SZ());
        cur          += new_fs_IO_SZ();
        size_aligned -= new_fs_IO_SZ();   
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return new_fs_ERROR_NONE;
}
/**
 * @brief 驱动写
 * 
 * @param offset 
 * @param in_content 
 * @param size 
 * @return int 
 */
int new_fs_driver_write(int offset, uint8_t *in_content, int size) {
    int      offset_aligned = new_fs_ROUND_DOWN(offset, new_fs_BLK_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = new_fs_ROUND_UP((size + bias), new_fs_BLK_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    new_fs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);
    
    // lseek(new_fs_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(new_fs_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // write(new_fs_DRIVER(), cur, new_fs_IO_SZ());
        ddriver_write(new_fs_DRIVER(), cur, new_fs_IO_SZ());
        cur          += new_fs_IO_SZ();
        size_aligned -= new_fs_IO_SZ();   
    }

    free(temp_content);
    return new_fs_ERROR_NONE;
}
/**
 * @brief 为一个inode分配dentry的bro，采用头插法
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int new_fs_alloc_dentry(struct new_fs_inode* inode, struct new_fs_dentry* dentry) {
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }
    else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;
    return inode->dir_cnt;
}
/**
 * @brief 分配一个inode，占用位图
 * 
 * @param dentry 该dentry指向分配的inode
 * @return new_fs_inode
 */
struct new_fs_inode* new_fs_alloc_inode(struct new_fs_dentry * dentry) {
    struct new_fs_inode* inode;
    int byte_cursor  = 0; 
    int bit_cursor   = 0; 
    int ino_cursor   = 0;
    int datano_cursor   = 0;
    int data_cnt = 0;
    boolean is_find_free_entry = FALSE;
    boolean is_find_enough_data = FALSE;

    /* 从索引位图中取空闲 */
    for (byte_cursor = 0; byte_cursor < new_fs_BLKS_SZ(new_fs_super.map_inode_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((new_fs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                new_fs_super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || ino_cursor == new_fs_super.max_ino)
        return -new_fs_ERROR_NOSPACE;

    /* 先按照要求分配一个 inode */
    inode = (struct new_fs_inode*)malloc(sizeof(struct new_fs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;

    /* 再从数据位图中取空闲，要取出若干个个位点，占据对应的值 */
    /*先遍历位图的大小1024B其中的B*/
    /*外层先遍历1024*/
    for (byte_cursor = 0; byte_cursor < new_fs_BLKS_SZ(new_fs_super.map_data_blks); 
         byte_cursor++)
    {
        /*内层遍历对应的8个比特位，进行异或操作*/
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((new_fs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                new_fs_super.map_data[byte_cursor] |= (0x1 << bit_cursor);

                /* 找到的块号立刻记入 inode，并判断找够了没 */
                inode->datano[data_cnt++] = datano_cursor;
                if(data_cnt == new_fs_DATA_PER_FILE){//预先分配四块数据块，对应的是占据四个数据位图位点，因此这里一旦找到了四块就可以结束了
                    is_find_enough_data = TRUE;
                    break;
                }
            }
            datano_cursor++;
        }
        if (is_find_enough_data) {
            break;
        }
    }

    if (!is_find_enough_data || datano_cursor == new_fs_super.max_data)
        return -new_fs_ERROR_NOSPACE;

    /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino   = inode->ino;
    
    /* inode指回dentry */
    inode->dentry = dentry;
    
    inode->dir_cnt = 0;
    inode->dentrys = NULL;
    //这里是最重要的，要预分配对应的data块给相应的inode块
    /* 对于文件，还需要预分配 pointer，指向内存中的随机块 */
    if (new_fs_IS_REG(inode)) {
        int p_count = 0;
        for(p_count = 0; p_count < new_fs_DATA_PER_FILE; p_count++){
            inode->block_pointer[p_count] = (uint8_t *)malloc(new_fs_BLK_SZ());
        }
    }

    return inode;
}
/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 * 
 * @param inode 
 * @return int 
 */
int new_fs_sync_inode(struct new_fs_inode * inode) {
    struct new_fs_inode_d  inode_d;
    struct new_fs_dentry*  dentry_cursor;
    struct new_fs_dentry_d dentry_d;
    int ino             = inode->ino;
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;

    int data_cnt;
    for(data_cnt = 0; data_cnt < new_fs_DATA_PER_FILE; data_cnt++){
        inode_d.datano[data_cnt] = inode->datano[data_cnt]; /* 数据块的块号也要赋值 */
    }
    int offset, offset_lim; 
    
    if (new_fs_driver_write(new_fs_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct new_fs_inode_d)) != new_fs_ERROR_NONE) {
        new_fs_DBG("[%s] io error\n", __func__);
        return -new_fs_ERROR_IO;
    }
                                                      /* Cycle 1: 写 INODE */
                                                      /* Cycle 2: 写 数据 */
    if (new_fs_IS_DIR(inode)) {      
        data_cnt = 0;                    
        dentry_cursor = inode->dentrys;
        /* dentry 要存满 4 个不连续的 blk 块 */
        while(dentry_cursor != NULL && data_cnt < new_fs_DATA_PER_FILE){
            offset = new_fs_DATA_OFS(inode->datano[data_cnt]); // dentry 从 inode 分配的首个数据块开始存
            offset_lim = new_fs_DATA_OFS(inode->datano[data_cnt] + 1);//下一个数据块块对应的偏移字节位置
            //通过offset和offset的大小比较来判断写入是否越界

            /* 写满一个 blk 时换到下一个 datano */
            while (dentry_cursor != NULL)
            {
                memcpy(dentry_d.fname, dentry_cursor->fname, new_fs_MAX_FILE_NAME);
                dentry_d.ftype = dentry_cursor->ftype;
                dentry_d.ino = dentry_cursor->ino;
                if (new_fs_driver_write(offset, (uint8_t *)&dentry_d, 
                                    sizeof(struct new_fs_dentry_d)) != new_fs_ERROR_NONE) {
                    new_fs_DBG("[%s] io error\n", __func__);
                    return -new_fs_ERROR_IO;                     
                }
                
                if (dentry_cursor->inode != NULL) {
                    new_fs_sync_inode(dentry_cursor->inode); /* 递归调用次函数 */
                }

                dentry_cursor = dentry_cursor->brother; /* dfs */
                offset += sizeof(struct new_fs_dentry_d);
                if(offset + sizeof(struct new_fs_dentry_d) > offset_lim)//预判一下下一个dentry的数据块是不是要超过当前的标号偏移数据块的大小
                    break;//如果超过则换下一个数据块进行写入
            }
            data_cnt++; /* 访问下一个指向的数据块 */
        }
    }
    else if (new_fs_IS_REG(inode)) {
        for(data_cnt = 0; data_cnt < new_fs_DATA_PER_FILE; data_cnt++){
            if (new_fs_driver_write(new_fs_DATA_OFS(inode->datano[data_cnt]), 
                    inode->block_pointer[data_cnt], new_fs_BLK_SZ()) != new_fs_ERROR_NONE) {
                new_fs_DBG("[%s] io error\n", __func__);
                return -new_fs_ERROR_IO;
            }
        }
    }
    return new_fs_ERROR_NONE;
}
/**
 * @brief 
 * 
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct new_fs_inode* 
 */
struct new_fs_inode* new_fs_read_inode(struct new_fs_dentry * dentry, int ino) {
    struct new_fs_inode* inode = (struct new_fs_inode*)malloc(sizeof(struct new_fs_inode));
    struct new_fs_inode_d inode_d;
    struct new_fs_dentry* sub_dentry; /* 指向 子dentry 数组 */
    struct new_fs_dentry_d dentry_d;
    int    data_cnt = 0; /* 用于读取多个 datano */
    int    dir_cnt = 0;/*计数表示当前已经读好的文件个数*/
    int    offset, offset_lim; /* 用于读取目录项 不连续的 dentrys */

    /*通过磁盘驱动来将磁盘中ino号的inode读入内存*/
    if (new_fs_driver_read(new_fs_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct new_fs_inode_d)) != new_fs_ERROR_NONE) {
        new_fs_DBG("[%s] io error\n", __func__);
        return NULL;
    }
    inode->dir_cnt = 0; /*不知道是文件还是目录，因此初始化为0*/
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    inode->dentry = dentry;     /* 指回父级 dentry*/
    inode->dentrys = NULL;
    for(data_cnt = 0; data_cnt < new_fs_DATA_PER_FILE; data_cnt++){
        inode->datano[data_cnt] = inode_d.datano[data_cnt]; /* 数据块的块号也要赋值 */
    }
    if (new_fs_IS_DIR(inode)) {
        dir_cnt = inode_d.dir_cnt;
        data_cnt = 0; /* 离散的块号 */
        
        while(dir_cnt != 0){/*硬盘内inode下的dentry全部被写入内存，也就是挂在了inode下*/
            offset = new_fs_DATA_OFS(inode->datano[data_cnt]); // dentry 从 inode 分配的首个数据块开始存
            offset_lim = new_fs_DATA_OFS(inode->datano[data_cnt] + 1);
            /* 写满一个 blk 时换到下一个 datano */
            while (offset + sizeof(struct new_fs_dentry_d) < offset_lim)
            {
                if (new_fs_driver_read(offset, (uint8_t *)&dentry_d, 
                                    sizeof(struct new_fs_dentry_d)) != new_fs_ERROR_NONE) {
                    new_fs_DBG("[%s] io error\n", __func__);
                    return NULL;                    
                }
                
                sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
                sub_dentry->parent = inode->dentry;
                sub_dentry->ino    = dentry_d.ino; 
                new_fs_alloc_dentry(inode, sub_dentry);

                offset += sizeof(struct new_fs_dentry_d);
                dir_cnt--;
                if(dir_cnt == 0) 
                    break;  /* 减到 0 后提前退出 */
            }
            data_cnt++; /* 访问下一个指向的数据块 */
        }
    }
     /*若是文件类型直接读取数据即可*/
    else if (new_fs_IS_REG(inode)) {
        /*将inode对应的数据块写入*/
        for(data_cnt = 0; data_cnt < new_fs_DATA_PER_FILE; data_cnt++){
            inode->block_pointer[data_cnt] = (uint8_t *)malloc(new_fs_BLK_SZ()); /* 只分配一个块 */
            if (new_fs_driver_read(new_fs_DATA_OFS(inode->datano[data_cnt]), inode->block_pointer[data_cnt], 
                                new_fs_BLK_SZ()) != new_fs_ERROR_NONE) {
                new_fs_DBG("[%s] io error\n", __func__);
                return NULL;                    
            }
        }
    }
    return inode;
}
/**
 * @brief 
 * 
 * @param inode 
 * @param dir [0...]
 * @return struct new_fs_dentry* 
 */
struct new_fs_dentry* new_fs_get_dentry(struct new_fs_inode * inode, int dir) {
    struct new_fs_dentry* dentry_cursor = inode->dentrys;
    int    cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt) {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}
/**
 * @brief 
 * 路径解析用的，返回对应匹配的dentry
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry 
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 * 
 * @param path 
 * @return struct new_fs_inode* 
 */
struct new_fs_dentry* new_fs_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct new_fs_dentry* dentry_cursor = new_fs_super.root_dentry;
    struct new_fs_dentry* dentry_ret = NULL;
    struct new_fs_inode*  inode; 
    int   total_lvl = new_fs_calc_lvl(path);
    int   lvl = 0;
    boolean is_hit;
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));
    *is_root = FALSE;
    strcpy(path_cpy, path);

    if (total_lvl == 0) {                           /* 根目录 */
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = new_fs_super.root_dentry;
    }
    fname = strtok(path_cpy, "/");       
    while (fname)
    {   
        lvl++;
        if (dentry_cursor->inode == NULL) {           /* Cache机制，如果没有可能是被换出了 */
            new_fs_read_inode(dentry_cursor, dentry_cursor->ino);
        }
        inode = dentry_cursor->inode;
        /*若遍历到的inode节点是FILE类型，则结束遍历*/
        if (new_fs_IS_REG(inode) && lvl < total_lvl) {
            new_fs_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }
         /*若遍历到的inode节点是目录类型*/
        if (new_fs_IS_DIR(inode)) {
            dentry_cursor = inode->dentrys;
            is_hit        = FALSE;

            while (dentry_cursor)
            {
                if (memcmp(dentry_cursor->fname, fname, strlen(fname)) == 0) {
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother; /* 遍历目录下的子文件 */
            }
            /*未找到匹配路径，给出报错*/
            if (!is_hit) {
                *is_find = FALSE;
                new_fs_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }
            /*找到完整匹配路径，返回对应的dentry*/
            if (is_hit && lvl == total_lvl) {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/"); 
    }

    /* 如果对应 dentry 的 inode 还没读进来，则重新读 */
    if (dentry_ret->inode == NULL) {
        dentry_ret->inode = new_fs_read_inode(dentry_ret, dentry_ret->ino);
    }
    
    return dentry_ret;
}
/**
 * @brief 挂载new_fs, Layout 如下
 * 
 * Layout
 * | Super | Inode Map | Data Map | Data |
 * 
 *  BLK_SZ = 2 * IO_SZ
 * 
 * 每个Inode占用一个Blk
 * @param options 
 * @return int 
 */
int new_fs_mount(struct custom_options options){
    int                 ret = new_fs_ERROR_NONE;
    int                 driver_fd;
    struct new_fs_super_d  new_fs_super_d;    /* 临时存放 driver 读出的超级块 */
    struct new_fs_dentry*  root_dentry;
    struct new_fs_inode*   root_inode;

    int                 super_blks;

    int                 inode_num;
    int                 data_num;
    int                 map_inode_blks;
    int                 map_data_blks;
    
    boolean             is_init = FALSE;

    new_fs_super.is_mounted = FALSE;

    // driver_fd = open(options.device, O_RDWR);
    driver_fd = ddriver_open(options.device);

    if (driver_fd < 0) {
        return driver_fd;
    }

    new_fs_super.driver_fd = driver_fd;
    ddriver_ioctl(new_fs_DRIVER(), IOC_REQ_DEVICE_SIZE,  &new_fs_super.sz_disk);  /* 请求查看设备大小 4MB */
    ddriver_ioctl(new_fs_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &new_fs_super.sz_io);    /* 请求设备IO大小 512B */
    new_fs_super.sz_blk = new_fs_super.sz_io * 2;                                 /* EXT2文件系统实际块大小 1024B */

    root_dentry = new_dentry("/", new_fs_DIR);

    /* 读取 super 到临时空间 */
    if (new_fs_driver_read(new_fs_SUPER_OFS, (uint8_t *)(&new_fs_super_d), 
                        sizeof(struct new_fs_super_d)) != new_fs_ERROR_NONE) {
        return -new_fs_ERROR_IO;
    }   
                                                      /* 读取super */
    if (new_fs_super_d.magic_num != new_fs_MAGIC_NUM) {     /* 幻数无，重建整个磁盘 */
                                                      /* 估算各部分大小 */
        
        /* 规定各部分大小 */
        super_blks      = new_fs_SUPER_BLKS;// super_blks = 1(超级块个数)
        map_inode_blks  = new_fs_MAP_INODE_BLKS;// map_inode_blks = 1(索引位图个数)
        map_data_blks   = new_fs_MAP_DATA_BLKS;// map_data_blks = 1 (数据位图个数)
        inode_num       = new_fs_INODE_BLKS;    //inode_num = 512( 索引块个数 )
        data_num        = new_fs_DATA_BLKS;     // data_num = 2048 (一个块挂载4个最大数据块个数2048)

                                                      /* 布局layout */
        new_fs_super.max_ino = inode_num;
        new_fs_super.max_data = data_num;
        // 偏移计算：//一个块是两个IO是1024B
        // 0| Super(1024) | Inode Map(2048) | DATA Map(3072) |Inode(527360) |DATA(3673088)|剩下的
        //上面不明显换算成下面的
        // 0| Super(1KB) | Inode Map(2KB) | DATA Map(3KB) |Inode(515KB) |DATA(3587KB)|剩下的
        new_fs_super_d.magic_num = new_fs_MAGIC_NUM;
        new_fs_super_d.map_inode_offset = new_fs_SUPER_OFS + new_fs_BLKS_SZ(super_blks);
        new_fs_super_d.map_data_offset  = new_fs_super_d.map_inode_offset + new_fs_BLKS_SZ(map_inode_blks);

        new_fs_super_d.inode_offset = new_fs_super_d.map_data_offset + new_fs_BLKS_SZ(map_data_blks);
        new_fs_super_d.data_offset  = new_fs_super_d.inode_offset + new_fs_BLKS_SZ(inode_num);

        new_fs_super_d.map_inode_blks  = map_inode_blks;
        new_fs_super_d.map_data_blks   = map_data_blks;

        new_fs_super_d.sz_usage        = 0;

        is_init = TRUE;
    }
    //原理同上
    new_fs_super.sz_usage   = new_fs_super_d.sz_usage;      /* 建立 in-memory 结构 */
    
    new_fs_super.map_inode = (uint8_t *)malloc(new_fs_BLKS_SZ(new_fs_super_d.map_inode_blks));
    new_fs_super.map_inode_blks = new_fs_super_d.map_inode_blks;
    new_fs_super.map_inode_offset = new_fs_super_d.map_inode_offset;

    new_fs_super.map_data = (uint8_t *)malloc(new_fs_BLKS_SZ(new_fs_super_d.map_data_blks));
    new_fs_super.map_data_blks = new_fs_super_d.map_data_blks;
    new_fs_super.map_data_offset = new_fs_super_d.map_data_offset;

    new_fs_super.inode_offset = new_fs_super_d.inode_offset;
    new_fs_super.data_offset = new_fs_super_d.data_offset;

    /* 读取两个位图到内存空间 */
    if (new_fs_driver_read(new_fs_super_d.map_inode_offset, (uint8_t *)(new_fs_super.map_inode), 
                        new_fs_BLKS_SZ(new_fs_super_d.map_inode_blks)) != new_fs_ERROR_NONE) {
        return -new_fs_ERROR_IO;
    }
    if (new_fs_driver_read(new_fs_super_d.map_data_offset, (uint8_t *)(new_fs_super.map_data), 
                        new_fs_BLKS_SZ(new_fs_super_d.map_data_blks)) != new_fs_ERROR_NONE) {
        return -new_fs_ERROR_IO;
    }

    if (is_init) {                                    /* 如果进行了重建，则分配根节点 */
        root_inode = new_fs_alloc_inode(root_dentry);    
        new_fs_sync_inode(root_inode);  /* 将重建后的 根inode 写回磁盘 */
    }
    
    /* 如果磁盘有数据，则先读入根结点，其他暂时不读 (Cache) */
    root_inode            = new_fs_read_inode(root_dentry, new_fs_ROOT_INO); 
    root_dentry->inode    = root_inode;
    new_fs_super.root_dentry = root_dentry;
    new_fs_super.is_mounted  = TRUE;

    return ret;
}
/**
 * @brief 
 * 
 * @return int 
 */
int new_fs_umount() {
    struct new_fs_super_d  new_fs_super_d; 

    if (!new_fs_super.is_mounted) {
        return new_fs_ERROR_NONE;
    }

    new_fs_sync_inode(new_fs_super.root_dentry->inode);     /* 从根节点向下刷写节点 */
                                                    
    new_fs_super_d.magic_num           = new_fs_MAGIC_NUM;
    new_fs_super_d.sz_usage            = new_fs_super.sz_usage;

    new_fs_super_d.map_inode_blks      = new_fs_super.map_inode_blks;
    new_fs_super_d.map_inode_offset    = new_fs_super.map_inode_offset;
    new_fs_super_d.map_data_blks       = new_fs_super.map_data_blks;
    new_fs_super_d.map_data_offset     = new_fs_super.map_data_offset;

    new_fs_super_d.inode_offset        = new_fs_super.inode_offset;
    new_fs_super_d.data_offset         = new_fs_super.data_offset;
    
    
    if (new_fs_driver_write(new_fs_SUPER_OFS, (uint8_t *)&new_fs_super_d, 
                     sizeof(struct new_fs_super_d)) != new_fs_ERROR_NONE) {
        return -new_fs_ERROR_IO;
    }
    /*将inode位图和data位图写入磁盘*/
    if (new_fs_driver_write(new_fs_super_d.map_inode_offset, (uint8_t *)(new_fs_super.map_inode), 
                         new_fs_BLKS_SZ(new_fs_super_d.map_inode_blks)) != new_fs_ERROR_NONE) {
        return -new_fs_ERROR_IO;
    }

    if (new_fs_driver_write(new_fs_super_d.map_data_offset, (uint8_t *)(new_fs_super.map_data), 
                         new_fs_BLKS_SZ(new_fs_super_d.map_data_blks)) != new_fs_ERROR_NONE) {
        return -new_fs_ERROR_IO;
    }

    free(new_fs_super.map_inode);
    free(new_fs_super.map_data);

     /*关闭驱动*/
    ddriver_close(new_fs_DRIVER());

    return new_fs_ERROR_NONE;
}