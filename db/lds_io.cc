#include <stdlib.h>
#include <stdio.h>

#include "db/lds_io.h"
#include "util/coding.h" //in LevelDB

#define MAGIC "LDSX"
extern char * OnlineMap; //lds.cc
extern uint64_t SlotTotal;

namespace leveldb {



size_t Slot_write(const void * ptr, size_t size, size_t count, LDS_Slot * slot ){
    //将起始地址为ptr, 长度为count字节的数据追加到slot所分配的对象中 
		//only write to LDS buffer
		uint32_t write_bytes;//payload
		write_bytes=size*count;//写数据的字节数目
		//printf("lds_io.cc, SLot_write, write_bytes=%d\n",write_bytes);
	
		if(slot->size + write_bytes > SLOT_SIZE){//如果超过阈值，报错
			//fprintf(stderr,"lds_io.cc, SLot_write, overflow,exit, name=%s, SLOT_SIZE:%d, slot->size=%d, write_bytes:%d\n",
                    //slot->file_name.c_str(), SLOT_SIZE, slot->size, write_bytes);
			
			exit(9);
		
		}
        //将追加的数据拷贝到分配给slot的数组
		memcpy(slot->buffer + slot->write_head,  ptr, write_bytes);
		
        //write_head记录了当前位置的偏移
		slot->write_head += write_bytes ;
		//当前slot存储的数据大小
        slot->size += write_bytes;
		//printf("lds_io.cc, SLot_write, size=%d\n",slot->size);

        //返回成功写入的字节数目
		return write_bytes;

}

size_t Log_write(const void * ptr, size_t size, size_t count, LDS_Log * log ){
    //日志写,包括MANIFEST文件，log文件，向LDS_Log追加日志对象
	/*This function append the construct the log objects*/
	//only write to LDS buffer

	uint32_t write_bytes;//payload，实际要写的字节数目
	write_bytes=size*count;
	//printf("lds_io.cc, Log_write, size=%d,data=%s\n", write_bytes,ptr);
	

	uint32_t header_size=20;//magic[4],type[4],sn[8],size[4];
	char head[header_size];
	sprintf(head,"%s",MAGIC);//魔数，保证日志对象的完整性
	EncodeFixed32(head+4, 2);//2 presents the type is common delta version，type
	EncodeFixed64(head+8, 0x333322);//log->sn
	EncodeFixed64(head+16, write_bytes);//size, 有效数据的长度
	
	uint32_t crc=0x77777777;//crc[4], call the crc32c::Extend(type_crc_[t], ptr, n) to calculate the crc，保证日志对象的完整性
	uint32_t crc_size=sizeof(crc);
	char crc_buf[crc_size];
	EncodeFixed32(crc_buf, crc);//将crc写入到crc_buf中
	
	if(log->file_name.find("MANIFEST")!=-1){//MANIFEST文件 
		//printf("lds_io.cc, Log_write, log->size=%d\n",  log->size);
		if(log->size +  header_size+write_bytes + crc_size > VERSION_LOG_SIZE){//超过阈值了
			//fprintf(stderr,"lds_io.cc,  Log_write,version area overflow\n");
			exit(9);
		}
	}
	else if(log->file_name.find(".log")!=-1){//LOG文件
		//printf("lds_io.cc, Log_write, log->size=%d\n",  log->size);
		if(log->size +  header_size+write_bytes + crc_size > BACKUP_SIZE){//超过阈值了
			//fprintf(stderr,"lds_io.cc, Log_write, backup area overflow\n");
			exit(9);
		}
	}
	
	memcpy(log->buffer + log->write_head,  head, header_size);//copy the header，拷贝头部

	memcpy(log->buffer + log->write_head+header_size,  ptr, write_bytes);//copy the payload， 拷贝有效数据

	

	memcpy(log->buffer + log->write_head+header_size+write_bytes,  crc_buf, crc_size);//copy the crc，拷贝crc冗余校验数据



	log->write_head += header_size+write_bytes + crc_size;//当前LDS_Log写位置
	
	log->size += header_size+write_bytes + crc_size;//当前LDS_log的大小

	

	return write_bytes;//返回写入的有效数据
	
}

size_t Slot_flush(LDS_Slot *slot){//slot下刷
		/*This function flushes the Chunk data to OS buffer
		*/
		//flush to OS buffer
		//printf("lds_io.cc, Slot_flush, begin\n");
		
		//文件偏移量设置为当前slot的物理偏移+slot的下刷偏移
		lseek64(slot->fd, slot->phy_offset+ slot->flush_offset, SEEK_SET);
		
        //下刷的字节数目设置为当前slot的写位置减去下刷偏移，也就是还没有下刷的slot长度
		uint64_t flush_bytes = slot->write_head - slot->flush_offset;
		
        //通过系统调用write,将fd的
		write(slot->fd, slot->buffer+ slot->flush_offset, flush_bytes);
		
		slot->flush_offset =  slot->write_head;
		
		//printf("lds_id.cc, Slot_flush, end, name=%s, slot->flush_offset=%d\n",slot->file_name.c_str(),slot->flush_offset);

		//exit(9);
		return flush_bytes;
		
}

size_t Slot_sync(LDS_Slot *slot){
    //同步，
	/*This function uses sync_file_range to sync the chunk data to the corresponding slot*/
	//flush to disk
	//printf("lds_io.cc, Slot_sync, begin, chun size=%d\n", slot->size);
	//printf("lds_io.cc, Slot_sync, begin, chun size=%d, flush offset=%d\n", slot->size, slot->flush_offset);
	//进行下刷
    Slot_flush(slot);

	//slot的最后8个字节用于确定字节的长度
    char coded_size[8];
	EncodeFixed64(coded_size, slot->size);

    //将文件偏移量定位到最后8个字节
	lseek64(slot->fd, slot->phy_offset+ (SLOT_SIZE -8), SEEK_SET);//8 bytes for the chunk size	
	//将sstable文件的长度写到最后8个字节
    write(slot->fd, coded_size, 8);
	
	int res;
    //将数据对应范围的的脏页刷回，而不是整个文件的范围
    //也就是只将slot的物理偏移量开始，SLOT_SIZE大小的脏页进行下刷
	res=sync_file_range(slot->fd, slot->phy_offset, SLOT_SIZE , SYNC_FILE_RANGE_WRITE|SYNC_FILE_RANGE_WAIT_AFTER );//SYNC_FILE_RANGE_WRITE
	//printf("lds_io.cc, Slot_sync, begin, name=%s, phyoffset=%d\n", slot->file_name.c_str(), slot->phy_offset );
	
	if(res!=0){	
		
		//fprintf(stderr,"lds_io.cc, Slot_sync, res error, exit\n");
		exit(3);
	}
	return res;
	//sleep(999);

	//exit(1);

}

size_t Slot_close(LDS_Slot *slot){
	/*Free the LDS buffer*/
	delete slot;//释放slot占用的空间
}

size_t Log_flush(LDS_Log * log){//下刷LDS_Log对象中还未下刷的数据
	/*
	 FLush the log object/objects to the OS buffer.
	 This function is called by LevelDB each time when a log request is processed.
	 */
	 //flush to OS buffer
	//printf("lds_id.cc, Log_flush, fd=%d\n", log->fd);
	
	/*Align to avoid the read-before-write problem*/
	int algin_unit=4096;//对齐单元，4kb
	uint64_t l_algined,r_aligned; //分为左对齐和右对齐
	l_algined=log->flush_offset;//上次下刷的偏移量
	r_aligned= log->write_head;//当前的写位置
	
	l_algined=l_algined/algin_unit;
	l_algined=l_algined*algin_unit;//左侧对齐到4KB 
	
	r_aligned=r_aligned/algin_unit;//右侧对其到4KB，需要+1
	r_aligned=(r_aligned+1)*algin_unit;
	
    //对齐后写入的数据量肯定比原本的大，只是为了对齐调用write，实际的位置是不变的
	/*Do the real flush operation with write system call*/
    //定位到对齐后相应的下刷偏移
	lseek64(log->fd, log->phy_offset + l_algined, SEEK_SET);
    //将数据通过write系统调用写到fd文件中
	write(log->fd, log->buffer+ l_algined, r_aligned- l_algined );//r_aligned- l_algined	

	//lseek64(log->fd, log->phy_offset+ log->flush_offset, SEEK_SET);//The lseek64 call can be removed to improve performance, if the log fd is only used by one logging procedure.

	uint64_t flush_bytes = log->write_head - log->flush_offset;
	
	//write(log->fd, log->buffer+ log->flush_offset, flush_bytes);
	
    //更新后的下刷偏移量
	log->flush_offset =  log->write_head;
	
	//printf("lds_id.cc, Log_flush, end\n");

	//exit(9);
    //返回成功下刷的字节数目
	return flush_bytes;
}

size_t Log_sync(LDS_Log * log){//log文件的同步
	//Commit the OS-buffered log objects.

	Log_flush(log);//首先下刷LDS_log中还未写下的
	
	//同步还未下刷的数据
	uint64_t sync_point=log->phy_offset +log->sync_offset;//同步的断点
	uint64_t sync_bytes=log->flush_offset-log->sync_offset;//同步的字节数目
	int res;
    //范围同步脏页
	res=sync_file_range(log->fd, sync_point, sync_bytes , SYNC_FILE_RANGE_WRITE|SYNC_FILE_RANGE_WAIT_AFTER );//SYNC_FILE_RANGE_WRITE
	//res=0;
    ////更新当前的同步偏移量
	log->sync_offset = log->flush_offset;
	
	
	return res;
	//res=sync_file_range(log->fd, slot->phy_offset, SLOT_SIZE , SYNC_FILE_RANGE_WRITE|SYNC_FILE_RANGE_WAIT_AFTER );	


}


size_t Log_close(LDS_Log * log){
	/*For interface compatibility*/
    //释放LDS_Log对象占用的内存空间
	delete log;
}
int decode(char *raw_data, LDS_Log *log){

	uint32_t header_size=20;//magic[4],type[4],sn[8],size[4];
	//printf("lds_io.cc, decode, begin\n");
	while(1){
		char magic[4];
        //首先是将设备上的信息拷贝到magic中
		strncpy(magic, raw_data,4);
		int cmp= strncmp(magic, MAGIC,4);//判断魔数是否相同
		//printf("lds_io.cc, decode, raw_data=%s,magic=%s, cmp=%d\n",raw_data,magic,cmp);
		if(cmp!=0){//如果不同那就失败退出
			break;
		}
        //获取有效字段的长度
		uint32_t payload_size= *(uint32_t*)(raw_data+16);
		//printf("lds_io.cc, decode, payload size=%d\n",payload_size);

		raw_data+= 20;
		//将有效字段从设备在读到LDS_Log对象的buffer中，读LDS_Log对象和写LDS_log对象是不同的
		memcpy(log->buffer + log->size, raw_data, payload_size);
        //设置LDS_log对象的长度
		log->size += payload_size;
		raw_data +=payload_size +4;//crc
	}

	

}
size_t Log_read(void * ptr, size_t size, size_t count, LDS_Log *log){
		/*Return raw data from the log area*/
		//printf("lds_io.cc, Log_read, begin, read_buf=%p\n",log->read_buf);
		if(log->read_buf==NULL){
			//printf("lds_io.cc, Log_read, buff is null, fd=%d, size=%lld\n",log->fd, log->size);
			//here first load the data to memory, decode the valid data and copy them to read_buf.
            //将LDS_log对象在设备上的空间内存映射到虚拟地址空间
			char *read_buf=  (char*)mmap(NULL, log->load_size, PROT_READ, MAP_SHARED, log->fd,log->phy_offset);
			//printf("lds_io.cc, Log_read, read_buf=%p,read_buf=%s\n",read_buf, read_buf );
			//解析read_buf中有效信息解析到log对象中
            decode(read_buf, log);

			//printf("lds_io.cc, Log_read, after decode, size=%lld,buffer=%s\n",log->size, log->buffer );
			
			//exit(9);
		}
		//now provide read service, content is stored in log->buffer, bytes are log->size. read position is at log->read_offset.

        //请求的数目
		size_t request_bytes= size*count;
		//剩余可读的数目
        size_t remained_bytes = log->size - log->read_offset;

        //供应的自己数，取小的
		size_t supply_bytes= remained_bytes > request_bytes? request_bytes : remained_bytes;
		if(supply_bytes>0){
            //从当前的读取偏移处继续读supply_bytes字节
			memcpy(ptr, log->buffer + log->read_offset, supply_bytes);
			//更新读取偏移
            log->read_offset += supply_bytes;
		}
		//printf("lds_io.cc, Log_read, end, supply_bytes=%d\n",supply_bytes);
		return supply_bytes;

}



uint64_t Alloc_slot(uint64_t next_file_number_){
    //分配新的编号
	//printf("lds_io.cc, Alloc_slot, begin\n");
	//this function will alloc a free slot number according to the online-map;
	//exit(9);
	
	uint64_t result;
	result = next_file_number_ %SlotTotal;//原本应该所对应的slot编号
	
	uint64_t final_number;
	uint64_t temp;
    //遍历在线位图，搜索空闲的slot，如果是空闲的，那就把这个slot编号分配出去，被占用了
    //从当前slot向后
	for(temp=result; temp< SlotTotal; temp++){//To confirm that the slot is free. If not, find the next following it.
		if(OnlineMap[temp]==0){//it is free
			OnlineMap[temp]==1;
			final_number= next_file_number_ + (temp-result);//reverse map
			return final_number;
		}
	
	}
    //没有找到

    //从最开始的第1个slot开始，向当前的slot遍历
	//fprintf(stderr,"lds_io.cc, Alloc_slot, round back\n");
	for(temp= 0 ;temp<result;temp++){ //round back
		if(OnlineMap[temp]==0){
			OnlineMap[temp]==1;
			final_number= next_file_number_ + (SlotTotal- result) + temp;
			
			return final_number;
		}
			
	}
	//if come to there, there is no free slot to alloc
	//fprintf(stderr,"lds_io.cc, storage full,exit!\n");
	exit(9);
}


uint64_t read_chunk_size(LDS_Slot *slot){
    //slot的物理偏移
	uint64_t offset= slot->phy_offset+ (SLOT_SIZE -8);
	
    	
	//printf("lds_io.cc, read_chunk_size,slot->phy_offset=%d, offset=%llu\n",slot->phy_offset,offset);
	char coded_size[8];
	//先同步整个sstable
	fsync(slot->fd);
	
	//sleep(1);
    //定位到相应的偏移
	lseek64(slot->fd, offset, SEEK_SET);//8 bytes for the chunk size
	//读取slot的最后8个字节，获取sstable的长度
    read(slot->fd, coded_size, 8);
	

	
	//printf("lds_io.cc, read_chunk_size,coded_size=%s\n",coded_size);

	uint64_t size =*(uint64_t*)coded_size;
	//返回chunk的长度
	return size;
	
}


}//end leveldb
