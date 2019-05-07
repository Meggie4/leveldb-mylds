#include <stdint.h>
#include <string>

#include <sys/types.h>
#include <sys/mman.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>    //provides O_RDONLY, 
#include <linux/fs.h>   //provides BLKGETSIZE
#include <sys/ioctl.h>  //provides ioctl()

#include <stdbool.h>

#include <vector>
#include <list>
#include <set>

#include "util/debug.h"

// #define OPEN_ARG

// #define PAGE_BYTES 1024  //1KB
// #define BLOCK_PAGES 8192 //4096 //4K pages a block
// #define BLOCK_BYTES 4194304 //16777216//4194304// 8388608 //4194304 //4MB
// #define SEGMENT_BLOCKS 1//#define AREA_BLOCKS 1 //1 block
// #define SEGMENT_BYTES (SEGMENT_BLOCKS*BLOCK_BYTES)
// #define VERSION_BYTES 4
// #define MAGIC "MMAPKV"
// #define MAGIC_BYTES 6

// #define FILE_NAME_BYTES 24 //file name
// #define FILE_ADDR_BYTES 8 //the segment offset
// #define FILE_BYTES 8 //content bytes
// #define ENTRY_BYTES 4096//(FILE_NAME_BYTES+FILE_ADDR_BYTES+..)
//segment
// #define EXTENDED_ENTRY_NUM_BYTE_OFFSET 22
// #define NON_LDB_ENTRY_NUM 32
// #define DEV_NAME_LENGTH 9 //  /dev/sdp/

// #define MMAP_PAGE 4096

// #define FLUSH_BYTES 4096

#define VERSION_LOG_SIZE 0x4000000 //100 0000 0000 0000 0000 0000 0000 //版本日志文件占用64MB
#define SLOT_SIZE 4194304	//插槽的大小为4MB，也就是一个sstable文件为4MB
#define BACKUP_SIZE (SLOT_SIZE*4)//备份的文件所占的总空间为16MB

namespace leveldb {

class LDS_Slot{//定义插槽
public:
    //physical address;//mmaped address
	//对应的映射地址
    char * addr;
    //对应的物理偏移量
	uint64_t phy_offset;
    //插槽的大小
	uint64_t size;

    //插槽存储的sstable文件名字
	std::string file_name;//for debug

	uint64_t write_head;//wirte head
	uint64_t flush_offset;//for flush to OS buffer
	uint64_t sync_offset;//for sync to disk

	void *buffer;//buffer data in  userspace 
	//对应的fd 
	int fd;

public:
    //根据一个sstable文件名来初始化一个插槽
	LDS_Slot(std::string name);

    //析构函数，释放buffer空间，由malloc分配
	~LDS_Slot(){
        /////////meggie
        if(buffer){
        /////////meggie
            DEBUG_T("LDS slot, to free buffer\n");
		    free(buffer);   
	    }
    }
};	

//定义的日志文件
class LDS_Log{

public:
    //映射到虚拟地址空间的地址
	char *addr;//mmaped address
	//物理的偏移量
    uint64_t phy_offset;
	uint64_t size;

    //日志的名字
	std::string file_name;//for debug

	uint64_t write_head;//
	uint64_t flush_offset;//for flush to OS buffer
	uint64_t sync_offset;//for sync to disk


	void* buffer;

	void *read_buf;
	uint64_t load_size;
	uint64_t read_offset;

	int fd;

	uint64_t sn;
public:
	LDS_Log(std::string name);
	
	~LDS_Log(){
        /////////meggie
        if(buffer){
            DEBUG_T("LDS log, to free buffer\n");
		    free(buffer);
        }
        /////////meggie
		
	}
};

//用于其他
class LDS_Others{
public:
	void* buffer;

	std::string file_name;//for debug

	uint64_t size;

public:
	LDS_Others(){
		size=0;
		//this->buffer=(char *)malloc(SEGMENT_BYTES);	
		posix_memalign(&(this->buffer),512,1000);//in order for direct IO.
		memset(this->buffer,0,1000);
		
	}
	~LDS_Others(){
        /////////meggie
        if(buffer)
        {
            DEBUG_T("LDS others, to free buffer\n");
		    free(buffer);
        }
        /////////meggie
	}


};

//整个的管理类
class LDS{

	public:
        //根据设备名来构造LDS对象
		LDS(const std::string& storage_path);
		LDS(const std::string& storage_path, int flash_using_exist);
        ~LDS() {
            if(version_fd != -1)
                close(version_fd);
            if(backup_fd != -1)
                close(backup_fd);
            if(slot_fd != -1)
                close(slot_fd);
        }
        //根据sstable名字分配一个插槽
		virtual LDS_Slot * alloc_slot(const std::string& chunk_name);
		//virtual LDS_Log * alloc_version(const std::string& name)=0;
		//virtual LDS_Log * alloc_backup(const std::string& name)=0;
		//分配一个log文件
        virtual LDS_Log * alloc_log(const std::string& name);

        
        //初始化该存储设备
		virtual int Storage_init(const std::string& storage_path);//e.g., /dev/sdb1
		//恢复该存储设备
        virtual int LDS_recover(const std::string& storage_path);//e.g., /dev/sdb1

	public:
		//char * online_map;
        //插槽的个数
		uint64_t slot_amount;

        //MANIFEST文件 
		LDS_Log *manifest;
        //备份的文件
		LDS_Log *backup;

		//int dev_fd;
		int version_fd;
		int backup_fd;
		int slot_fd;

		char *dev_read_only;
		//大小
        uint64_t size;


};






}
