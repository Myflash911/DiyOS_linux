#ifndef _DIYOS_FAT12_H
#define _DIYOS_FAT12_H
typedef unsigned long long      u64; //64bit长度
typedef unsigned int            u32; //32bit长度
typedef unsigned short          u16; //16bit长度
typedef unsigned char           u8;  //8bit长度

//修改对齐方式是必须的，否则会按照缺省对齐
//比如64位机器就会按8字节对齐
//    32位机器就会按4字节对齐
#pragma pack (1) /*指定按1字节对齐*/ 
//printf("sizeof(struct BPB):%d\n", sizeof(struct BPB));  按1字节对齐时，结果是25
//printf("sizeof(struct BPB):%d\n", sizeof(struct BPB));  按4字节对齐时，结果是28
/**
 * @struct BPB
 * @brief FAT12 Boot Parameter Block
 * BPB位于引导扇区偏移11Byte处
 *		之前有:
 *			BS_jmpBOOT		3Byte
 *			BS_OEMName		8Byte
 */
struct BPB {
	u16 bytes_per_sec;		//每扇区字节数  
	u8  sec_per_clus;		//每簇扇区数  
	u16 rsvd_sec_cnt;		//Boot记录占用的扇区数  
	u8  num_fats;			//FAT表个数  
	u16 root_ent_cnt;		//根目录最大文件数  
	u16 tot_sec16;			//扇区总数
	u8  media;				//介质描述符
	u16 fat_sz16;			//每FAT扇区数  
	u16 sec_per_trk;		//每磁道扇区数
	u16 num_heads;			//磁头数
	u32 hidd_sec;			//隐藏扇区数
	u32 tot_sec32;			//如果BPB_FATSz16为0，该值为FAT扇区数  
	//以下数据不需要持久化到软盘
	int i_cnt;				//引用次数
};

//1字节对齐的时候struct BPB大小
//#define SIZE_OF_BPB			25

//根目录条目  
struct fat12_dir_entry {
	char name[8];			//文件名8字节，扩展名3字节
	char suffix[3];
	u8   attr;				//文件属性  
	char reserved[10];		//保留位
	u16  wrt_time;			//最后一次写入时间
	u16  wrt_date;			//最后一次写入日期
	u16  fst_clus;			//开始簇号  
	u32  file_size;			//文件大小
};

//1字节对齐的时候struct fat12_dir_entry大小
//#define SIZE_OF_ROOTENTRY	32
#pragma pack () /*取消指定对齐，恢复缺省对齐*/ 

#define is_dir(entry_ptr)       ((entry_ptr)->attr & 0x10) != 0

#define FAT_TAB_BASE_SEC(bpb_ptr) ((bpb_ptr)->rsvd_sec_cnt + (bpb_ptr)->hidd_sec)

#define FAT_TAB_SIZE(bpb_ptr) (((bpb_ptr)->fat_sz16>0?(bpb_ptr)->fat_sz16:(bpb_ptr)->tot_sec32) * (bpb_ptr)->bytes_per_sec)

#define FAT_TAB_BASE(bpb_ptr) (FAT_TAB_BASE_SEC((bpb_ptr)) * (bpb_ptr)->bytes_per_sec)
#define ROOT_ENT_BASE_SEC(bpb_ptr) (FAT_TAB_BASE_SEC((bpb_ptr))+ (bpb_ptr)->num_fats * ((bpb_ptr)->fat_sz16>0?(bpb_ptr)->fat_sz16:(bpb_ptr)->tot_sec32))

#define ROOT_ENT_BASE(bpb_ptr) (ROOT_ENT_BASE_SEC((bpb_ptr)) * (bpb_ptr)->bytes_per_sec)

/**
 * @define DATA_BASE_SEC
 * @brief 数据区起始扇区
 */
#define DATA_BASE_SEC(bpb_ptr) (ROOT_ENT_BASE_SEC((bpb_ptr)) + ((bpb_ptr)->root_ent_cnt * sizeof(struct fat12_dir_entry) + (bpb_ptr)->bytes_per_sec - 1)/(bpb_ptr)->bytes_per_sec)

/**
 * @define DATA_BASE
 * @brief 某簇数据区起始位置
 * @param bpb_ptr BPB指针
 * @param clus_no簇号
 * 还跟簇有关
 */
#define DATA_BASE(bpb_ptr, clus_no) ((DATA_BASE_SEC((bpb_ptr)) + (clus_no) - 2) * (bpb_ptr)->sec_per_clus * (bpb_ptr)->bytes_per_sec)

/**
 * @define VALIDATE
 * @brief 判断目录项是否合法
 * @param dir_entry fat12文件系统的目录项
 * @return 0 合法 1 非法
 */
#define VALIDATE(dir_entry) ((validate((dir_entry)->name, 11) || validate((dir_entry)->suffix, 3)) || (dir_entry)->fst_clus <= 0 || (dir_entry)->file_size < 0)

/**
 * @define CLUS_BYTES
 * @brief 每个簇所占字节数
 */
#define CLUS_BYTES(bpb_ptr) ((bpb_ptr)->sec_per_clus * (bpb_ptr)->bytes_per_sec)

#define MAX(a, b) ((a)>(b)?(a):(b))

#define MIN(a, b) ((a)<(b)?(a):(b))

#endif
