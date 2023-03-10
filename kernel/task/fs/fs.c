/******************************************************/
/*                 操作系统的文件系统                 */
/******************************************************/
//现在处理起来有些问题：
//1. 最开始设计的文件系统只有 / 一级目录，所以close的时候只对文件的inode->i_cnt自减， 而如今变成多级树形目录，在open过程中会对路径上所有的目录文件的i_cnt自增，所以应该修改close函数，对所有目录文件inode->i_cnt自减
//	 在struct inode中添加了一个i_parent属性，用来保存父目录inode ptr , put_inode会递归的追溯i_parent进行清理
//2. 大量地方用到了strip_path 获取文件名和目录inode指针，同时用search_file获取文件的inode idx，而search_file中也使用了strip_path函数，这会导致i_cnt多加了一次，应该重新设计一下这两个函数，避免出现这样的错误
//	 合并search_file 和 strip_path ,统一使用search_file函数，调用完search_file后，根据具体需求调用put_inode
//要用到search_file的函数有:
//		do_open ：调用之后如果失败，需要CLEAR_INODE， 否则不需要CLEAR_INODE，一直保存inode直到对应fd调用do_close()
//		do_unlink: 调用之后无论是否失败，都需要CLEAR_INODE
//		do_rmdir : 调用之后无论失败，都需要CLEAR_INODE
//		do_stat:	调用之后无论失败，都需要CLEAR_INODE
#include "type.h"
#include "syscall.h"
#include "global.h"
#include "assert.h"
#include "hd.h"
#include "floppy.h"
#include "fs.h"
#include "tortoise.h"
#include "fat12.h"
#include "stdio.h"
#include "stat.h"
#include "proc.h"
/**
 * @function get_afs
 * @brief 根据设备号获取抽象文件系统
 * @param inode
 * @return 抽象文件系统 ptr
 */
static struct abstract_file_system * get_afs(struct inode *inode);
static struct abstract_file_system * get_afs_by_dev(int dev);

static void init_fs();

static void init_tty_files(struct abstract_file_system * afs, struct inode *pin);
static void init_block_dev_files(struct abstract_file_system * afs, struct inode *pin);

/** 文件操作 **/
static int do_open(struct message *p_msg);
static int do_close(struct message *p_msg);
static int do_rdwt(struct message *p_msg);
static int do_seek(struct message *p_msg);
static int do_tell(struct message *p_msg);
static int do_unlink(struct message *p_msg);
static int do_stat(struct message *p_msg);
static int do_rename(struct message *p_msg);
/** 目录操作 **/
static int do_mkdir(struct message *p_msg);
static int do_rmdir(struct message *p_msg);
static int do_chdir(struct message *p_msg);
/** 挂载操作 **/
static int do_mount(struct message *p_msg);
static int do_unmount(struct message *p_msg);
/** 辅助process fork **/
static int fs_fork(struct message *msg);
static int fs_exit(struct message *msg);
//只有在do_open中创建文件的时候使用
//static struct inode * create_file(char *path, int flags);
//使用strip_path返回的结果，避免重复调用strip_path
static struct inode * create_directory(char *path, int flags);

static int strip_path(char *filename, const char *pathname, struct inode **ppinode);
//添加参数，避免重复调用strip_path
static int search_file(const char *path, char * filename, struct inode **ppinode);
/**
 * Main Loop 
 */
void task_fs()
{
	init_fs();
	struct message msg;
	while(1){
		//wait for other process
		send_recv(RECEIVE, ANY, &msg);
		int src = msg.source;
		switch(msg.type){
			case OPEN:
				//open 要返回FD
				msg.FD = do_open(&msg);					//已经适配fat12
				break;
			case CLOSE:
				//返回是否成功
				msg.RETVAL = do_close(&msg);			//不需要适配fat12
				break;
			case READ:
			case WRITE:
				//读写文件
				msg.CNT = do_rdwt(&msg);				//已经适配fat12
				break;
			case STAT:
				//获取文件状态
				msg.RETVAL = do_stat(&msg);				//不需要适配fat12
				break;
			case SEEK:
				//seek
				msg.RETVAL = do_seek(&msg);				//不需要适配fat12
				break;
			case TELL:
				//tell
				msg.POSITION = do_tell(&msg);			//不需要适配fat12
				break;
			case UNLINK:
				//删除普通文件
				msg.RETVAL = do_unlink(&msg);			//已经适配fat12
				break;
			case RENAME:
				//重命名文件
				msg.RETVAL = do_rename(&msg);			//TODO
			case MKDIR:
				//创建空目录
				msg.RETVAL = do_mkdir(&msg);			//已经适配fat12
				break;
			case RMDIR:
				//删除空目录
				msg.RETVAL = do_rmdir(&msg);			//已经适配fat12
				break;
			case CHDIR:
				//修改进程所在目录
				msg.RETVAL = do_chdir(&msg);			//不需要适配fat12
				break;
			case MOUNT:
				//挂载文件系统
				msg.RETVAL = do_mount(&msg);			//因为只有rootfs和fat12两个文件系统，不存在嵌套挂载， 暂时不需要适配fat12
				break;
			case UNMOUNT:
				//卸载文件系统
				msg.RETVAL = do_unmount(&msg);			//因为只有rootfs和fat12两个文件系统，不存在嵌套挂载， 暂时不需要适配fat12
				break;
			case RESUME_PROC:
				//恢复挂起的进程
				src = msg.PID; //恢复进程,此时将src变成TTY进程发来的消息中的PID
				break;
			case FORK:
				//fork 让子进程共享父进程的文件fd
				msg.RETVAL = fs_fork(&msg);
				break;
			case EXIT:
				//子进程退出时的文件操作
				msg.RETVAL = fs_exit(&msg);
				break;
			default:
				panic("invalid msg type:%d\n", msg.type);
				break;
		}
		//返回
		if(msg.type != SUSPEND_PROC){
			msg.type = SYSCALL_RET;
			send_recv(SEND, src, &msg);
		}
		//如果收到的msg.type == SUSPEND_PROC那么直接继续循环
	}
}

struct abstract_file_system * get_afs_by_dev(int dev)
{
	struct abstract_file_system * ret;
	switch(dev){
        case ROOT_DEV:
                ret = &tortoise;
                break;
        case FLOPPYA_DEV:
                ret = &fat12;
                break;
        default:
                assert(0);
                break;
        }
        return ret;

}
struct abstract_file_system * get_afs(struct inode *inode)
{
	int dev = inode != 0?inode->i_dev:ROOT_DEV;
	return get_afs_by_dev(dev);
}

/**
 * @function init_fs
 * @brief 初始化文件系统
 */
void init_fs()
{
	int i;
	//f_desc_table[]
	for(i=0;i<MAX_FILE_DESC_COUNT;i++){
		memset(&f_desc_table[i], 0, sizeof(struct file_desc));
	}
	//inode_table[]
	for(i=0;i<MAX_INODE_COUNT;i++){
		memset(&inode_table[i], 0, sizeof(struct inode));
	}
	//init
	init_tortoise();
	init_fat12();
	//初始化根目录设备
	struct abstract_file_system * afs = get_afs(0);
	afs->init_fs(ROOT_DEV);
	
	//创建/dev目录
	struct inode *dir_inode = root_inode();
	dir_inode = afs->create_directory(dir_inode, "dev", O_CREATE);
	//struct inode *p_dir_inode = create_directory("/dev", O_CREATE);
	//创建设备文件
	assert(afs->create_special_file != 0);
	assert(dir_inode!=0);
	init_tty_files(afs, dir_inode);
	//init block device files
	init_block_dev_files(afs, dir_inode);
	put_inode(dir_inode);
	assert(dir_inode->i_cnt == 0);
}


/**
 * @function int_tty_files
 * @brief 初始化tty设备文件
 * @return
 */
void init_tty_files(struct abstract_file_system * afs, struct inode *dir_inode)
{
	//step 1 创建tty0 tty1 tty2三个tty设备文件
	//tty字符设备一定存在，所以不需要检测
	char filename[MAX_PATH];
	int i;
	struct inode * newino;
	for(i=0;i<CONSOLE_COUNT;i++){
		newino = 0;
		sprintf(filename, "tty%d", i);
		newino = afs->create_special_file(dir_inode, filename, I_CHAR_SPECIAL, O_CREATE, MAKE_DEV(DEV_CHAR_TTY, i));
		assert(newino != 0);
		//put_inode(newino);
		//这里不要用put_inode，否则连同dir_inode也会被清理
		//在最后put_inode(dir_inode)的时候就会出错
		newino->i_cnt--; //直接减少引用计数即可，因为这是内核级创建文件
				 //无须跟进程绑定，也就无须占用inode table
	}
		
}

/**
 * @function init_block_dev_files
 * @brief 初始化块设备文件
 * @return
 */
void init_block_dev_files(struct abstract_file_system * afs, struct inode *dir_inode)
{
	//软盘驱动设备也是一定存在的
	//软盘是否存在，要在挂载的时候判断
	struct inode *newino = 0;
	newino = afs->create_special_file(dir_inode, "floppy", I_BLOCK_SPECIAL, O_CREATE, FLOPPYA_DEV);
	assert(newino != 0);
	//put_inode(newino);
	//同init_tty_files
	//直接减少引用计数，不用调用put_inode函数
	newino->i_cnt--;
}

/**
 * @function rw_sector
 * R/W a sector via messaging with the corresponding driver
 *
 * @param io_type	DEV_READ or DEV_WRITE
 * @param dev		device number
 * @param pos		Byte offset from/to where to r/w
 * @param size		How many bytes
 * @param pid		To whom the buffer belongs.
 * @param buf		r/w buffer.
 *
 * @return Zero if success.
 */
int rw_sector(int io_type, int dev, u64 pos, int bytes, int pid, void*buf)
{
	assert(io_type == DEV_READ || io_type == DEV_WRITE);
	struct message msg;
	msg.type	= io_type;
	msg.DEVICE	= MINOR(dev);
	msg.POSITION	= pos;
	msg.BUF		= buf;
	msg.CNT		= bytes;
	msg.PID		= pid;
	assert(dd_map[MAJOR(dev)].driver_pid != INVALID_DRIVER);
	send_recv(BOTH, dd_map[MAJOR(dev)].driver_pid, &msg);
	return 0;
}



/**
 * @function do_open
 * @brief open file
 * @param p_msg msg address
 *
 * @return File descriptor if successful, otherwise a negative error code.
 */
int do_open(struct message *p_msg)
{
	int fd = -1;
	char pathname[MAX_PATH];
	int flags = p_msg->FLAGS;
	int name_len = p_msg->NAME_LEN;
	int src = p_msg->source;
	struct process *pcaller = proc_table + src;
	assert(name_len<MAX_PATH);
	memcpy((void*)va2la(TASK_FS, pathname), (void*)va2la(src, p_msg->PATHNAME), name_len);
	pathname[name_len] = 0;
	//find a free slot in PROCESS:filp[]
	int i;
	for(i=0;i<MAX_FILE_COUNT;i++){
		if(pcaller->filp[i] == 0){
			fd = i;
			break;
		}
	}
	if((fd<0)||(fd>=MAX_FILE_COUNT)){
		panic("filp[] is full (PID:%d)", src);
	}
	//find a free slot in f_desc_table[]
	for(i=0;i<MAX_FILE_DESC_COUNT;i++){
		if(f_desc_table[i].fd_inode == 0){
			break;
		}
	}
	if(i>=MAX_FILE_DESC_COUNT){
		panic("f_desc_table[] is full (PID:%d)", src);
	}
	char filename[MAX_PATH];
	struct inode *dir_inode = 0, *pin=0;
	int inode_nr = search_file(pathname, filename, &dir_inode);
	if(inode_nr == INVALID_PATH){
		goto label_fail;
	}
	//原来这里是这样写的，但是上面inode_nr==-1的时候直接跳到label_fail
	//而下面pin变量并没有被初始化，所以label_fail中对应的pin变量的值是未知的
	//会产生问题
	//struct inode *pin = 0;
	if(flags & O_CREATE){
		//创建文件
		if(inode_nr>0){
			//printk("file exists.\n");
			//put_inode(dir_inode);
			//return -1;
			goto label_fail;
		} else {
			//pin = create_file(filename, dir_inode, flags);
			pin = get_afs(dir_inode)->create_file(dir_inode, filename, flags);
		}
	} else {
		assert(flags & O_RDWT);
		if(inode_nr==0){
			//printk("file does not exist.\n");
			//return -1;
			goto label_fail;
		}
		//pin = get_inode(dir_inode, inode_nr);
		pin = get_afs(dir_inode)->get_inode(dir_inode, inode_nr);
	}
	if(pin){
		if(pin->i_mode == I_DIRECTORY){
			//不能打开目录文件
			//释放inode table中的缓存
			//put_inode(pin);
			//return -1;
			goto label_fail;
		}
		//connects proc with file_descriptor
		pcaller->filp[fd] = &f_desc_table[i];
		//connects file_descriptor with inode
		f_desc_table[i].fd_inode = pin;
		f_desc_table[i].fd_mode = flags;
		f_desc_table[i].fd_pos = 0;
		f_desc_table[i].fd_cnt = 1;
		int imode = pin->i_mode & I_TYPE_MASK;
		if(imode == I_CHAR_SPECIAL) {
			//设备文件 dev_tty0 dev_tty1 dev_tty2
			struct message driver_msg;
			driver_msg.type = DEV_OPEN;
			int dev = pin->i_start_sect;
			driver_msg.DEVICE=MINOR(dev);
			//@see kernel/global.c dd_map[]
			//4 is index of dd_map
			//dd_map[4] = TASK_TTY
			assert(MAJOR(dev) == 4);
			assert(dd_map[MAJOR(dev)].driver_pid != INVALID_DRIVER);
			//printk("FS send message to %d\n", dd_map[MAJOR(dev)].driver_pid);
			send_recv(BOTH, dd_map[MAJOR(dev)].driver_pid, &driver_msg);
			assert(driver_msg.type==SYSCALL_RET);
		} else if (imode == I_BLOCK_SPECIAL) {
			//打开块设备文件
			//之前考虑open块设备时发送DEV_OPEN消息
			//现在改为mount时发送DEV_OPEN消息
			/*
			struct message driver_msg;
			driver_msg.type = DEV_OPEN;
			int dev = pin->i_start_sect;
			driver_msg.DEVICE=MINOR(dev);
			//dd_map[1] = TASK_FLOPPY
			//assert(MAJOR(dev)==1); //可能有多个块设备
			assert(dd_map[MAJOR(dev)].driver_pid != INVALID_DRIVER);
			send_recv(BOTH, dd_map[MAJOR(dev)].driver_pid, &driver_msg);
			assert(driver_msg.type==SYSCALL_RET);
			*/
			assert(imode == I_BLOCK_SPECIAL);
		} else if (imode == I_DIRECTORY) {
			//assert(pin->i_num == ROOT_INODE);
			assert(0); //不会到这里
		} else {
			assert(imode == I_REGULAR);
		}
	} else {
		//return -1;
		goto label_fail;
	}
	return fd;
label_fail:
	CLEAR_INODE(dir_inode, pin);
	return -1;
}

/**
 * @function do_seek
 * @brief 
 */
int do_seek(struct message *p_msg)
{
	int fd = p_msg->FD;
	int offset = p_msg->OFFSET;
	int where = p_msg->WHERE;
	if((offset<0 && where == 0) || (offset>0 && where == 2)){
		//where:0 SEEK_START 偏移只能为正
		//where:2 SEEK_END   偏移只能为负
		return -1;
	}
	int src = p_msg->source;
	struct process *pcaller = proc_table + src;
        assert((pcaller->filp[fd]>=f_desc_table) & (pcaller->filp[fd] < f_desc_table + MAX_FILE_DESC_COUNT));
        if(!(pcaller->filp[fd]->fd_mode & O_RDWT)){
                return -1;
        }
	struct inode *pin = pcaller->filp[fd]->fd_inode;
	assert(pin>=inode_table && pin<inode_table + MAX_INODE_COUNT);
	if(pin->i_cnt == 0) {
		//说明fd没有对应的inode
		return -1;
	}
	int imode = pin->i_mode & I_TYPE_MASK;
	if(imode!=I_REGULAR){
		return -1;//只对普通文件有效
	}
	int file_size = pin->i_size;	
        int pos = pcaller->filp[fd]->fd_pos;
	int new_pos = 0;
	switch(where){
	case 0:
		new_pos = offset;
		break;
	case 1:
		new_pos = pos + offset;
		break;
	case 2:
		new_pos = file_size + offset; //之前已经判断过 offset一定为负数
		break;
	default:
		break;
	}
	if(new_pos<0||new_pos>file_size){
		return -1; //invalid new pos
	}
	//update pos
	pcaller->filp[fd]->fd_pos = new_pos;
	return 0;	
}

/**
 * @function do_tell
 * @brief 文件位置
 */
int do_tell(struct message *p_msg)
{
	int fd= p_msg->FD;
	int src = p_msg->source;
	struct process *pcaller = proc_table + src;
    assert((pcaller->filp[fd]>=f_desc_table) & (pcaller->filp[fd] < f_desc_table + MAX_FILE_DESC_COUNT));
    if(!(pcaller->filp[fd]->fd_mode & O_RDWT)){
        return -1;
    }
	struct inode * pin = pcaller->filp[fd]->fd_inode;
    assert(pin>= inode_table && pin<inode_table + MAX_INODE_COUNT);
	if(pin->i_cnt == 0) {
		//说明fd没有对应的inode
		return -1;
	}
    int imode = pin->i_mode & I_TYPE_MASK;
	if(imode!=I_REGULAR){
		//只能对普通文件进行操作
		return -1;
	}
        int pos = pcaller->filp[fd]->fd_pos;
	return pos;
}

/**
 * @function do_close
 * @brief  关闭文件
 * 
 *  @param p_msg
 *  
 */
int do_close(struct message *p_msg)
{
	int fd = p_msg->FD;
	int src = p_msg->source;
	struct process *pcaller = proc_table + src;
	if(pcaller->filp[fd]->fd_inode->i_cnt == 0) {
		//说明fd没有对应的inode
		return -1;
	}
	//释放inode 资源
	put_inode(pcaller->filp[fd]->fd_inode);
	if(--pcaller->filp[fd]->fd_cnt == 0){
		pcaller->filp[fd]->fd_inode = 0;
	}
	//清空filp指向的f_desc_table中某一表项的fd_inode指针，归还f_desc_table的slot
	pcaller->filp[fd]->fd_inode = 0;
	//归还process中的filp slot
	pcaller->filp[fd] = 0;
	return 0;
}

/**
 * @function do_rdwt
 * @brief 读写文件
 * 
 * @param p_msg 消息指针
 *
 * @return 返回读写的字节数
 */
int do_rdwt(struct message *p_msg)
{
	int fd = p_msg->FD; //file descriptor
	void *buf = p_msg->BUF; //r/w buffer user's level
	int len = p_msg->CNT;//r/w bytes
	int src = p_msg->source;
	struct process *pcaller = proc_table + src;
	assert((pcaller->filp[fd]>=f_desc_table) & (pcaller->filp[fd] < f_desc_table + MAX_FILE_DESC_COUNT));
	if(!(pcaller->filp[fd]->fd_mode & O_RDWT)){
		return -1;
	}
	int pos = pcaller->filp[fd]->fd_pos;
	struct inode * pin = pcaller->filp[fd]->fd_inode;
	assert(pin>= inode_table && pin<inode_table + MAX_INODE_COUNT);
	int imode = pin->i_mode & I_TYPE_MASK;
	if(imode == I_CHAR_SPECIAL){
		int t = p_msg->type ==READ?DEV_READ:DEV_WRITE;
		p_msg->type = t;
		int dev = pin->i_start_sect;
		assert(MAJOR(dev) == 4);
		p_msg->DEVICE =  MINOR(dev);
		p_msg->BUF = buf;
		p_msg->CNT = len;
		p_msg->PID = src;
		assert(dd_map[MAJOR(dev)].driver_pid != INVALID_DRIVER);
		send_recv(BOTH, dd_map[MAJOR(dev)].driver_pid, p_msg);
		assert(p_msg->CNT == len);
		//assert(p_msg->type == SYSCALL_RET);
		//收到的消息不是都是SYSCALL_RET类型的，TTY read返回SUSPEND_PROC
		return p_msg->CNT;
	} else if(imode == I_BLOCK_SPECIAL){
		//读写块设备
		//这里设定可以读块设备内的raw数据
		//if(p_msg->type == READ) {
			//read函数理论上可以直接读取软盘
			//但是这没什么意义，而且read传入的count可以是任意Byte，而我们floppy的DEV_OPEN处理函数一般要求512Byte为单位
			//所以当对BLOCK 文件read/write时，直接忽略，不错操作
		//} else {
			//write函数中不可以直接写入软盘
		//}
		return 0; //直接返回0byte
	}else {
		assert(pin->i_mode == I_REGULAR || pin->i_mode == I_DIRECTORY);
		assert((p_msg->type == READ)||(p_msg->type == WRITE));
		struct abstract_file_system *afs = get_afs(pin);
		if(p_msg->type == READ){
			return afs->rdwt(fd, src, DEV_READ, pin, buf, pos, len);
		} else {
			return afs->rdwt(fd, src, DEV_WRITE, pin, buf, pos,len);
		}
	}
}

/**
 * @function do_unlink
 * @brief  释放inode slot
 *
 * @param p_msg
 *
 * @return 0 successful, -1 error
 */
int do_unlink(struct message *p_msg)
{
	char pathname[MAX_PATH];
	//get parameters from the message;
	int name_len = p_msg->NAME_LEN;
	int src = p_msg->source;
	assert(name_len<MAX_PATH);
	memcpy((void*)va2la(TASK_FS, pathname), (void*)va2la(src, p_msg->PATHNAME), name_len);
	pathname[name_len]=0;
	
	if(strcmp(pathname, "/") == 0){
		printk("FS:do_unlink():: cannot unlink the root\n");
		return -1;
	}
	
	char filename[MAX_PATH];
	struct inode *dir_inode = 0;
	struct inode *pin = 0;
	int inode_nr = search_file(pathname, filename, &dir_inode);
	//if(inode_nr == INVALID_INODE || dir_inode == 0){ //dir_inode == 0的时候一定是返回INVALID_PATH的时候
	// 这里为了判断的统一，采用以下形式
	if(inode_nr == INVALID_INODE || inode_nr == INVALID_PATH){
		//file not found
		printk("FS::do_unlink():: search_file() return invalid inode: %s\n", pathname);
		//return -1;
		goto label_fail;
	}
	pin = get_afs(dir_inode)->get_inode(dir_inode, inode_nr);
	if(pin == 0){
		goto label_fail;
	}
	//更新afs到pin的dev，否则遇到挂载点出现问题
	//afs = get_afs(pin);
	if(pin->i_mode != I_REGULAR){
		//can only remove regular files
		//if you want remove directory  @see do_rmdir
		printk("cannot remove file %s, because it is not a regular file.\n", pathname);
		//return -1;
		goto label_fail;
	}
	if(pin->i_cnt>1){
		//thie file was opened
		printk("cannot remove file %s, because pin->i_cnt is %d.\n", pathname, pin->i_cnt);
		//return -1;
		goto label_fail;
	}
	//这里pin->i_cnt应该是1
	assert(pin->i_cnt == 1);
	//unlink_file内部会进行put_inode操作
	return get_afs(pin)->unlink_file(pin);
label_fail:
	CLEAR_INODE(dir_inode, pin);
	return -1;
}


/**
 * @function do_mkdir
 * @brief 创建目录
 * @param p_msg
 * @return
 */
int do_mkdir(struct message *p_msg)
{
	char pathname[MAX_PATH];
	int name_len = p_msg->NAME_LEN;
	int src = p_msg->source;
	assert(name_len<MAX_PATH);
	memcpy((void*)va2la(TASK_FS, pathname), (void*)va2la(src, p_msg->PATHNAME), name_len);
	pathname[name_len] = 0;
	struct inode *dir_inode = create_directory(pathname, O_CREATE);
	if(dir_inode == 0){
		return -1;
	} else {
		put_inode(dir_inode);
		return 0;
	}
}

/**
 * @function do_rmdir
 * @brief 删除空目录
 * @param p_msg
 * @return
 */
int do_rmdir(struct message *p_msg)
{
	char pathname[MAX_PATH];
	int src = p_msg->source;
	int name_len = p_msg->NAME_LEN;
	assert(name_len<MAX_PATH);
	memcpy((void*)va2la(TASK_FS, pathname), (void*)va2la(src, p_msg->PATHNAME), name_len);
	pathname[name_len] = 0;
	if(strcmp(pathname, "/") == 0){
		return -1;//根目录不能删除
	}
	//只有这里使用的逻辑就不单独提出一个函数了
	char filename[MAX_PATH];
	struct inode *dir_inode = 0; //父目录inode指针
	int dir_entry_count; //目录项数量
	int inode_idx = search_file(pathname, filename, &dir_inode);
	//if(inode_idx == INVALID_INODE || dir_inode == 0){
	if(inode_idx == INVALID_INODE || inode_idx == INVALID_PATH){
		//file not found
		//return -1;
		goto label_fail;
	}
	struct inode *pinode = get_afs(dir_inode)->get_inode(dir_inode, inode_idx);
	if(!pinode){
		//return -1;
		goto label_fail;
	}
	if(pinode->i_mode != I_DIRECTORY){
		//不是目录文件
		//return -1;
		goto label_fail;
	}
	if(pinode->i_cnt>1){
		//open函数不能用于目录文件，所以目录的pinode->i_cnt一般恒为1
		//但是考虑mount的时候可以将pinode->i_cnt 加1，所以这里也判断一下,防止删除挂载点
		printk("pathname:%s,inode:%d,i_cnt:%d\n", pathname,pinode->i_num,pinode->i_cnt);
		//return -1;
		goto label_fail;
	}
	//由于判读按目录是否为空在rootfs和fat12下的方法是不同的，所以即使在unlink_file中已经适配过fat12了，
	//还是要额外在这里判读一下是否是空目录
	//HOOK POINT
	if(!get_afs(pinode)->is_dir_empty(pinode)){
		//不是空目录
		goto label_fail;
	}
	//删除空目录
	//unlink_file 内部会进行put_inode 操作
	return get_afs(pinode)->unlink_file(pinode);
label_fail:
	CLEAR_INODE(dir_inode, pinode);
	return -1;
}

/**
 * @function do_chdir
 * @brief 修改进程所在目录
 * @param p_msg
 * @return 0 if success
 */
int do_chdir(struct message *p_msg)
{
	char pathname[MAX_PATH];
	int src = p_msg->source;
	int name_len = p_msg->NAME_LEN;
	assert(name_len<MAX_PATH);
	memcpy((void*)va2la(TASK_FS, pathname), (void*)va2la(src, p_msg->PATHNAME), name_len);
	pathname[name_len] = 0;
	//
	char filename[MAX_PATH]; //出参参数
	struct inode *dir_inode = 0; //父目录inode指针
	int inode_idx;
	inode_idx = search_file(pathname, filename, &dir_inode);
	if(inode_idx == INVALID_INODE || inode_idx == INVALID_PATH){
		//无效的目录
		goto label_fail;
	}
	struct inode *pinode = get_afs(dir_inode)->get_inode(dir_inode, inode_idx);
	if(!pinode){
		goto label_fail;
	}
	if(pinode->i_mode != I_DIRECTORY){
		//不是目录
		goto label_fail;
	}
	//是一个合法目录
	struct process *pcaller = proc_table + src;
	//清空原来的current_path_inode
	if(pcaller->current_path_inode != INVALID_INODE){
		//pcaller->current_path_inode == 0
		//说明原来没有设置进程运行目录
		//pcaller->current_path_inode != INVALID_INODE (0)
		//说明原来设置过进程运行目录，那么需要清理目录inode的引用计数
		put_inode(pcaller->current_path_inode);
	}
	pcaller->current_path_inode = pinode; //设置新的进程目录
	return 0;
label_fail:
	CLEAR_INODE(dir_inode, pinode);
	return -1;
}


/**
 * @function fs_fork
 * @brief Perform the aspects of fork() that relate to files
 * @param msg message ptr
 * @return Zero if success, otherwise a negative integer.
 */
int fs_fork(struct message *msg)
{
	int i;
	struct process *child = &proc_table[msg->PID];
	//current_path_inode 也需要fork
	struct inode * pinode = child->current_path_inode;
	//init进程会保证设置current_path_inode,所以所有子进程都会有current_path_inode
	assert(pinode!=0)
	while(pinode != 0){
		pinode->i_cnt ++;
		pinode = pinode->i_parent;
	}
	for(i=0;i<MAX_FILE_COUNT;i++){
		if(child->filp[i]){
			child->filp[i]->fd_cnt++;
			child->filp[i]->fd_inode->i_cnt++;
		}
	}
	return 0;
}

/**
 * @function fs_exit
 * @brief  Perform the aspects of exit() that relate to files
 *
 */
 
int fs_exit(struct message *msg)
{
	int i;
	struct process *p = &proc_table[msg->PID];
	//current_path_inode 也需要修改
	put_inode(p->current_path_inode);
	p->current_path_inode = 0;
	for(i=0;i<MAX_FILE_COUNT;i++){
		//release the inode
		p->filp[i]->fd_inode->i_cnt--;
		if(--p->filp[i]->fd_cnt == 0){
			p->filp[i]->fd_inode = 0;
		}
		p->filp[i] = 0;
	}
	return 0;
}


/**
 * @function create_directory
 * @brief 创建新的目录inode, 并设置磁盘上的数据
 * @param path 目录
 * @param  flags 预留
 * @return inode指针
 */
struct inode * create_directory(char *path, int flags)
{
	char filename[MAX_PATH];
	struct inode * dir_inode = 0;
	struct inode * pinode= 0;
	int inode_idx = search_file(path, filename, &dir_inode);
	if(inode_idx == INVALID_PATH){
		//说明目录链断开
		//创建目录的位置非法
		goto label_fail;		
	}
	struct abstract_file_system *afs = get_afs(dir_inode);
	if(inode_idx!=INVALID_INODE){
		pinode = afs->get_inode(dir_inode, inode_idx);
		if(pinode==0 || dir_inode == 0){
			//return 0;
			goto label_fail;
		}
		if(pinode->i_mode == I_DIRECTORY){
			return pinode; //说明已经存在目录
		} else {
			//return 0; //父目录下有同名文件
			goto label_fail;
		}
	}
	return afs->create_directory(dir_inode, filename, O_CREATE);
label_fail:
	CLEAR_INODE(dir_inode, pinode);
	return 0;
}

/**
 * @function strip_path
 * @brief Get the basename from the fullpath
 *  in current DIYOS v0.2 FS v1.0, all files are stored in the root directory.
 *  there is no sub-folder thing
 *  this routine should be called at the very beginning of file operations
 *  such as open(), read() and write(). It accepts the full path and returns
 *  tow things: the basename and a ptr of the root dir's i-node
 * 
 * e.g. After strip_path(filename, "/blah", ppinode) finishes, we get:
 *	- filename: "blah"
 *	- *ppinode: root_inode
 *	- ret val: 0(successful)
 *
 * currently an acceptable pathname should begin with at most one '/'
 * preceding a filename.
 *
 * Filenames may contain any character except '/' and '\\0'
 *
 * It can work correctlly when path contains "." or ".."
 *
 * @param[out] filename the string for the result
 * @param[in] pathname the full pathname.
 * @param[out] ppinode the ptr of the dir's inode will be stored here.
 *
 * @return dir_inode_idx if success, otherwise the pathname is not valid.
 */
int strip_path(char *filename, const char *pathname, struct inode **ppinode)
{
	const char * s = pathname;
	struct dir_entry *pde;
	struct fat12_dir_entry *fat12pde;
	struct inode* pinode;
	char * t;
	int i,j;
	if(*s==0){
		//空字符串非法
		return -1;
	}
	if(*s!='/'){
		//必须是绝对路径
		return -1;
	}
	s++;
	*ppinode=root_inode(); //从root inode开始寻找
	int dir_entry_count_per_sect = SECTOR_SIZE/DIR_ENTRY_SIZE;
	int dir_entry_count, dir_entry_blocks_count;
	while(*s!=0){
		pinode = 0;
		t = filename;
		while(*s!='/' && *s!=0){
			*t++=*s++;
		}
		*t=0;
		if(*s!=0) s++; //skip /
		int inode_idx = get_afs(*ppinode)->get_inode_num_from_dir(*ppinode, filename);
		if(inode_idx != INVALID_INODE){
			pinode = get_afs(*ppinode)->get_inode(*ppinode, inode_idx);
		}
		if(*s!=0){
			//不是最后一级目录
			if(pinode==0){
				//1.目录不存在get_inode返回
				//2.没进入循环 初始值即为0
				//return -1;
				goto label_fail;
			}
			if(pinode->i_mode!=I_DIRECTORY){
				//不是目录
				//return -1;
				goto label_fail;
			}
			*ppinode=pinode;		
		} else {
			if(pinode!=0 && pinode->i_cnt>0){
				//需要将最后一层的i_cnt还原
				//因为只有在search_file中会调用strip_path方法
				//而search_file最后还要对最后一级的文件进行cnt++操作
				//所以这里先还原
				pinode->i_cnt--;
			}
		}
	}
	return 0;
label_fail:
	//put_inode(*ppinode); 不在这里清理，最外层函数统一清理
	return -1;
}



/**
 * @function search_file
 * @brief Search the file and return the inode_idx
 * @param[in] path the full path of the file to search
 * @param[out] filename the filename of the file to search
 * @param[out] ppinode the directory's inode ptr
 * @return Ptr to the inode of the file if successful
 * 			Otherwise INVAID_INODE (0): 表示沿着目录链正确的找到dir_inode,但是目录项没有对应文件
 * 					  INVALID_PATH (-1): 表示没有沿着目录链找到正确的位置
 * @ses open()
 * @ses do_open()
 */
int search_file(const char *path, char *filename, struct inode **ppinode)
{
	//fast path
	if(*path == '/' && *(path+1) == 0){
		//root
		*ppinode = 0;
		return ROOT_INODE;
	}
	int i,j;
	if(strip_path(filename, path, ppinode)!=0){
		return INVALID_PATH;
	}
	struct inode * dir_inode = *ppinode;
	if(filename[0] == 0){//path:"/"
		return dir_inode->i_num;
	}
	return get_afs(dir_inode)->get_inode_num_from_dir(dir_inode, filename);
}


/**
 * @function put_inode
 * @brief Decrease the reference bumber of a slot in inode_table.
 * when the nr reaches zero  it means the inode is not used any more and can be overwritten by a new inode
 * @param pinode inode ptr
 */
void put_inode(struct inode *pinode)
{
	assert(pinode && pinode->i_cnt>0);
	/*
	if(pinode==0 || pinode->i_cnt<=0){
		return;
	}
	*/
	pinode->i_cnt --;
	if(pinode->i_parent && pinode->i_parent->i_cnt>0){
		put_inode(pinode->i_parent);
	}
}

/**
 * @function do_stat
 * @brief 处理stat消息
 * @param p_msg 消息体
 * @return 
 */
int do_stat(struct message *p_msg)
{
	int ret = -1;
	char pathname[MAX_PATH];
	//get parameters from the message;
	int name_len = p_msg->NAME_LEN;
	int src = p_msg->source;
	assert(name_len<MAX_PATH);
	memcpy((void*)va2la(TASK_FS, pathname), (void*)va2la(src, p_msg->PATHNAME), name_len);
	pathname[name_len]=0;
	char filename[MAX_PATH];
	struct inode *dir_inode = 0, *pinode = 0;
	struct stat _stat; //内核层保存结果
	int inode_idx = search_file(pathname,filename, &dir_inode);
	//if(inode_idx == INVALID_INODE || dir_inode == 0){
	if(inode_idx == INVALID_INODE || inode_idx == INVALID_PATH){
		//file not found
		printk("FS::do_stat():: search_file() return invalid inode: %s\n", pathname);
		ret = -1;
		goto label_clear_inode;
	}
	//TODO 这里get_inode有点问题，每次都是i_cnt==0导致每次都从磁盘重新load出来
	//inode_table[] 只是一个缓存，就算重新load，也不会影响i_size
	//而且奇怪的是随后的read还是可以读出数据的
	pinode = get_afs(dir_inode)->get_inode(dir_inode, inode_idx);
	if(!pinode){
		printk("FS::do_stat:: get_inode() return invalid inode: %s\n", pathname);
		ret = -1;
		goto label_clear_inode;
	}
	_stat.st_dev = pinode->i_dev;
	_stat.st_ino = pinode->i_num;
	_stat.st_mode = pinode->i_mode;
	if(pinode->i_mode == I_CHAR_SPECIAL||pinode->i_mode == I_BLOCK_SPECIAL){
		_stat.st_dev = pinode->i_start_sect;
	} else {
		_stat.st_rdev = 0;
	}
	_stat.st_size = pinode->i_size;
#ifdef _WITH_TEST_
	_stat.i_cnt = pinode->i_cnt - 1;
#endif
	memcpy((void*)va2la(src, p_msg->BUF), (void*)va2la(TASK_FS, &_stat), sizeof(struct stat)); //数据拷贝到用户层的buf中
	ret = 0;
label_clear_inode:
	CLEAR_INODE(dir_inode, pinode);
	return ret;
}

/**
 * @function do_rename
 * @brief 重命名文件
 * @param p_msg 消息体
 * @return 0 if success
 */
int do_rename(struct message *p_msg)
{
	int inode_idx = 0, ret = -1;
	struct inode *old_pinode = 0, *old_dir_inode = 0, *new_pinode = 0, *new_dir_inode = 0;
	char oldfilename[MAX_PATH]; //作为search_file 出参使用
	char newfilename[MAX_PATH]; //作为search_file 出参使用
	char oldname[MAX_PATH]; //内核进程空间内
	int oldname_len = p_msg->OLDNAME_LEN;
	assert(oldname_len < MAX_PATH);
	char newname[MAX_PATH]; //内核进程空间内
	int newname_len = p_msg->NEWNAME_LEN;
	assert(newname_len < MAX_PATH);
	//将数据从用户态复制到内核态
	int src = p_msg->source;
	memcpy((void*)va2la(TASK_FS, oldname), (void*)va2la(src, p_msg->OLDNAME), oldname_len);
	oldname[oldname_len] = 0;
	
	//判断oldname是否存在
	inode_idx = search_file(oldname, oldfilename,&old_dir_inode);
	//if(inode_idx == INVALID_INODE || old_dir_inode == 0){
	if(inode_idx == INVALID_INODE || inode_idx == INVALID_PATH){
		//oldname的目录存在错误
		goto label_clear_inode;
	}
	old_pinode = get_afs(old_dir_inode)->get_inode(old_dir_inode , inode_idx);
	if(!old_pinode){
		//oldname不存在
		goto label_clear_inode;
	}
	//判断是否是目录或者普通文件类型
	if(old_pinode->i_mode != I_DIRECTORY && old_pinode->i_mode != I_REGULAR){
		//不是目录也不是普通文件
		goto label_clear_inode;
	}
	// 考虑并发？TASK_FS 一次只能处理一个其他进程来的消息，所以对于文件的操作是串行化的，所以不需要考虑并发加锁
	// 但是要考虑已有其他进程打开过oldname文件的情况，这种情况应该立刻返回错误
	if(old_pinode->i_cnt>1){
		//说明已经有其他进程打开oldname文件了，不能修改文件名
		goto label_clear_inode;
	}
	//判断newname目录是否正确，并且newname不存在
	memcpy((void*)va2la(TASK_FS, newname), (void*)va2la(src, p_msg->NEWNAME), newname_len);
	newname[newname_len] = 0;
	inode_idx = search_file(newname, newfilename, &new_dir_inode);
	if(inode_idx != INVALID_INODE){
		//newname目录错误 inode_idx == INVALID_PATH
		//或者 已经存在同名的文件 inode_idx >0
		goto label_clear_inode;
	}
	//HOOK POINT
	//考虑挂载fat12文件系统的情况下的rename比较复杂
	//1. 同一个文件系统下，可以简单的修改目录项
	//2. 不同文件系统下，需要移动文件数据
	if(old_pinode->i_dev == new_dir_inode->i_dev){
		// 在相同的文件系统内
		// 修改文件名(移动树形目录的子树节点)
		// 由于TASK_FS一次只处理其他进程的单个消息，所以以下操作是原子的
		//修改的时候，也要修改pinode的parent链
		// 先在newname所在目录下创建目录项
		get_afs(new_dir_inode)->new_dir_entry(new_dir_inode, old_pinode->i_num, newfilename);
		// 再将oldname所在目录的目录项删除
		get_afs(old_dir_inode)->rm_dir_entry(old_dir_inode, old_pinode->i_num);
		//修改parent inode指针, 否则下面CLEAR_INODE的时候会出错
		new_pinode = old_pinode;
		new_pinode->i_parent = new_dir_inode;
		old_pinode = 0;
		ret = 0;
	} else {
		//在不同的文件系统内
	}
label_clear_inode:
	CLEAR_INODE(old_dir_inode, old_pinode);
	CLEAR_INODE(new_dir_inode, new_pinode);
	return ret;
}

/**
 * @function do_mount
 * @brief 处理MOUNT消息
 * @param p_msg
 * @return 
 */
int do_mount(struct message *p_msg)
{
	int error = -1;
	int inode_idx = 0;
	char filename[MAX_PATH];
	char pathname[MAX_PATH];
	struct inode *target_pinode = 0, *target_dir_inode = 0, *source_pinode = 0, *source_dir_inode = 0;
	int path_name_len = p_msg->NAME_LEN;
	assert(path_name_len < MAX_PATH);
	char devname[MAX_PATH];
	int dev_name_len = p_msg->DEVNAME_LEN;	
	assert(dev_name_len < MAX_PATH);
	int src = p_msg->source;
	memcpy((void*)va2la(TASK_FS, pathname), (void*)va2la(src, p_msg->PATHNAME), path_name_len);
	pathname[path_name_len] = 0;
	//step1  判断挂载目录是否是目录
	inode_idx = search_file(pathname, filename, &target_dir_inode);
	//if(inode_idx == INVALID_INODE || target_dir_inode == 0){
	if(inode_idx == INVALID_INODE || inode_idx == INVALID_PATH){
		//target的目录不对
		error = 1;
		goto label_fail;
	}
	target_pinode = get_afs(target_dir_inode)->get_inode(target_dir_inode, inode_idx);
	if(!target_pinode){
		//target 文件不存在
		error = 2;
		goto label_fail;
	}
	if(target_pinode->i_mode != I_DIRECTORY){
		//不是目录文件
		error = 3;
		goto label_fail;
	}
	if(target_pinode->i_cnt > 1){
		//该目录下有文件被打开或该目录是某进程的运行时目录
		error = 4;
		goto label_fail;
	}
	//step2 判断是否已经挂载
	//TODO 
	memcpy((void*)va2la(TASK_FS, devname), (void*)va2la(src, p_msg->DEVNAME), dev_name_len);
	devname[dev_name_len] = 0;
	//step3 判断是否是设备文件
	inode_idx = search_file(devname, filename, &source_dir_inode);
	//if(inode_idx == INVALID_INODE || source_dir_inode == 0){
	if(inode_idx == INVALID_INODE || inode_idx == INVALID_PATH){
		//dev 路径无效
		error = 5;
		goto label_fail;
	}
	source_pinode = get_afs(source_dir_inode)->get_inode(source_dir_inode, inode_idx);
	if(!source_pinode){
		//无效dev文件
		error = 6;
		goto label_fail;
	}
	if(source_pinode->i_mode != I_BLOCK_SPECIAL){
		//不是设备文件
		error = 7;
		goto label_fail;
	}
	//source_pinode->i_dev 被挂载到的文件系统设备号
	//target_pinode 挂载点目录
	int target_dev = source_pinode->i_start_sect;
	get_afs_by_dev(target_dev)->mount(target_pinode, target_dev);
	CLEAR_INODE(source_dir_inode, source_pinode);
	return 0;
label_fail:
	CLEAR_INODE(target_dir_inode, target_pinode);
	CLEAR_INODE(source_dir_inode, source_pinode);
	return error;
}


/**
 * @function do_unmount
 * @brief 处理UNMOUNT消息
 * @param p_msg
 * @return 
 */
int do_unmount(struct message *p_msg)
{
	char pathname[MAX_PATH];
	int name_len = p_msg->NAME_LEN;
	assert(name_len < MAX_PATH);
	int src = p_msg->source;
	memcpy((void*)va2la(TASK_FS, pathname), (void*)va2la(src, p_msg->PATHNAME), name_len);
	pathname[name_len] = 0;
	//验证
	//step1
	struct inode *dir_inode = 0, *pinode = 0;
	char filename[MAX_PATH];
	int inode_idx = search_file(pathname, filename, &dir_inode);
	//if(inode_idx == INVALID_INODE || dir_inode == 0){
	if(inode_idx == INVALID_INODE || inode_idx == INVALID_PATH){
		//路径无效
		goto label_fail;
	}
	pinode = get_afs(dir_inode)->get_inode(dir_inode, inode_idx);
	if(!pinode){
		//无效文件
		goto label_fail;
	}
	if(pinode->i_mode != I_DIRECTORY){
		//卸载不是目录
		goto label_fail;
	}
	//TODO 判断是否被挂载过
	if(pinode->i_dev == ROOT_DEV){
		//目录dev是ROOT_DEV ,说明没有被挂在过
		goto label_fail;
	}
	//清理设备，并且将inode设置新的dev
	get_afs(pinode)->unmount(pinode, ROOT_DEV);
	//清理pinode
	CLEAR_INODE(dir_inode, pinode);
	return 0;
label_fail:
	CLEAR_INODE(dir_inode, pinode);
	return -1;
}
