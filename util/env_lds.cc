// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <deque>
#include <set>
#include "leveldb/env.h"
#include "leveldb/slice.h"
#include "port/port.h"
#include "util/logging.h"
#include "util/mutexlock.h"
#include "util/posix_logger.h"



#include "db/lds_io.h"

////////meggie
std::string dev_name = "/dev/sdb7";
////////meggie
extern  int flash_using_exist;//0 is write 1 is read



namespace leveldb {



namespace{//for class

class LDS_WritableSlot : public WritableFile {//用于LDS_slot对象的写，将数据写到LDS_Slot对象中

	std::string chunk_name_;//sstable文件的名字
	LDS_Slot *slot_;//LDS_Slot对象 
	public:
        //定义LDS_WritableSlot对象 
		LDS_WritableSlot(const std::string& chunk_name, LDS_Slot* slot) : chunk_name_(chunk_name), slot_(slot) { 

		}
		
    //追加写
	virtual Status Append(const Slice& data) {
			//1.for regual write, LDS just append the data at the current offset. However, LDS must be informed if the write is the tail, so that LDS can align the tail to the right end of the slot.
			//2. an alternate way is using the last block of the slot as slot meta. But LDS still needs to be informed for the last write of the slot, so that it can update the meta.
			//3. LDS_Slot can maintain logical offset to provide compatible read function.
		//将其实地址为data.data(), 长度为data.size()的数据追加写到slot对象的buffer中
        size_t r=Slot_write(data.data(), 1, data.size(), slot_);
		return Status::OK();
	}

	virtual Status Close() {
		//printf("env_lds.cc, LDS_WritableSlot,Close\n");
        //释放slot对象所分配的内存 
		Slot_close(slot_);
		return Status::OK();
		
	}

	 virtual Status Flush() {
	 
        //将slot对象还未flush的数据进行flush 
		Slot_flush(slot_);
			
		return Status::OK();

	}
	 virtual Status Sync() {
	
         //进行同步 
		//here we know that the slot is finished, so we add the chunk size to the right-end of the slot.
		Slot_sync(slot_);
		return Status::OK();

	}
};

class LDS_WritableLog : public WritableFile {//用于log对象的写，将数据追加写到log的对象的buffer中 

	std::string log_name_;
	LDS_Log *log_;
	public:
		LDS_WritableLog(const std::string& name_, LDS_Log* log) : log_name_(name_), log_(log) { 

		}
		

    ////日志追加写
	virtual Status Append(const Slice& data) {//called by log_writer。
				//1. By design, the log should be packged by LDS, however, we currently use the LevelDB package. For the version log, we just append it(by 512B in direct IO). For the backup log, we use normal way.

				
		//printf("env_lds, Append, data=%s\n",data.data());
		//printf("test,exit, name=%s\n",log_name_.c_str());
		//exit(9);
	   
        //将起始地址为data.data()，长度为data.size()的数据追加写到log对象的buffer缓冲区	
        size_t r=Log_write(data.data(), 1, data.size(), log_);
		return Status::OK();
	}

	virtual Status Close() {
		return Status::OK();
		
	}

	 virtual Status Flush() {
		//printf("env_lds, LDS_WritableLog, Flush, log_name_=%s\n",log_name_.c_str());
		Log_flush(log_);
		return Status::OK();

	}
	 virtual Status Sync() {
		//printf("env_lds, LDS_WritableLog, Sync, log_name_=%s\n",log_name_.c_str());
		Log_sync(log_);
		return Status::OK();

	}
};


class LDS_WritableOthers : public WritableFile {//其他文件的追加写

	std::string name_;
	FILE *nul_;
	public:
		LDS_WritableOthers(const std::string& name_, FILE* nul) : name_(name_), nul_(nul) { 

		}
		

	virtual Status Append(const Slice& data) {
		//printf("env_lds, Append, data=%s\n",data.data());
		//size_t r=Log_write(data.data(), 1, data.size(), log_);
		return Status::OK();
	}

	virtual Status Close() {
		return Status::OK();
		
	}

	 virtual Status Flush() {
			
		return Status::OK();

	}
	 virtual Status Sync() {
		return Status::OK();

	}
};

class LDS_MmapedSlot : public RandomAccessFile {//slot对象的随机访问 
	std::string name_;//sstable的名字
	void* mmapped_region_;//mmap的起始地址
	size_t length_;//长度

	public:
        //传入sstable文件名，映射的起始地址，映射的长度
		LDS_MmapedSlot(const std::string& name, void* base, size_t length): name_(name), mmapped_region_(base), length_(length) {

		}

        //从偏移量为offset处读取长度为n的字节，并将信息保存到scrach中，并赋值给result的data()
		virtual Status Read(uint64_t offset, size_t n, Slice* result,char* scratch) const {
			//the tail's logical offset will be adjacent with data blocks, this is maintained internal the LDS_Slot and black to leveldb

			//printf("env_lds.cc, LDS_MmapedSlot, read, begin,mmapped_region_=%p\n",mmapped_region_);
			//printf("env_lds.cc, LDS_MmapedSlot, length_=%d, offset=%d,n=%d\n",length_,offset,n );

			
			*result = Slice(reinterpret_cast<char*>(mmapped_region_) + offset, n);

			//printf("env_lds.cc, LDS_MmapedSlot, mmapped_region_=%s\n",mmapped_region_+offset);
			//exit(9);

			return Status::OK();
		}
		
		virtual Status Skip(uint64_t n) {
			
			return Status::OK();
		}

};




class LDS_SequantialLog : public SequentialFile {//log对象的顺序读
	std::string name_;//log文件如MANIFEST，.log文件的名字 
	LDS_Log *log_;//log对象 
	public:
        //顺序的log文件 
		LDS_SequantialLog(const std::string& name, LDS_Log *log) : name_(name), log_(log)  { 

		}

        //顺序读取n个字节到scrach中 
		virtual Status Read(size_t n, Slice* result, char* scratch) {
			Status s;
				//here we will decide the valid version logs for manifest. the content returned to leveldb is like a whole file.
				//leveldb will fill the online-map and record the valid point of backup log.
			//printf("env_lds.cc, LDS_SequantialLog, Read,begin, name=%s, n=%d\n", log_->file_name.c_str(),n);

            //从log对象的当前位置顺序读取n个字节 
			size_t r =Log_read(scratch, 1, n, log_);//the read string will be pointed by scratch

			//printf("env_lds.cc, LDS_SequantialLog, after, Read, r=%d\n",r);

			*result = Slice(scratch, r);
			//exit(9);
			return s;

		}
		
		virtual Status Skip(uint64_t n) {
			//if (fseek(file_, n, SEEK_CUR)) {
			 // return IOError(filename_, errno);
			//}
			return Status::OK();
		}
};

class LDS_SequentialOthers : public SequentialFile {//其他的读取

	std::string name_;
	LDS_Others *oth_;
	int current_flag;
	public:
		LDS_SequentialOthers(const std::string& name, LDS_Others* oth) : name_(name), oth_(oth)  { 
			//printf("env.lds,LDS_SequentialOthers,LDS_SequentialOthers,name_=%s\n",name_.c_str());
			if(name_.find("CURRENT")!=-1){
				char *content="MANIFEST-LDS";
				memcpy(oth_->buffer, content, strlen(content));
				oth_->size=strlen(content);
				current_flag=0;

			}
		}
		
	virtual Status Read(size_t n, Slice* result, char* scratch) {
		Status s;
		//printf("env.lds,LDS_SequentialOthers,Read, name_=%s\n",name_.c_str());
		if(name_.find("CURRENT")!=-1){//current file
			//printf("env.lds,LDS_SequentialOthers,Read , read CURRENT\n");
			if(current_flag==0){
				scratch="MANIFEST-LDS\n";
				current_flag=1;//only once needed for CURRENT;
			}
			else{
				scratch="";
			}
			size_t r=strlen(scratch);
			*result = Slice(scratch, r);

		}

		return s;
	}

	virtual Status Skip(uint64_t n) {
		//if (fseek(file_, n, SEEK_CUR)) {
		 // return IOError(filename_, errno);
		//}
		return Status::OK();
	}
};

}//name space classes

class LDSEnv : public Env {
 public:
	 LDSEnv();
	virtual ~LDSEnv(){


	}

    //根据文件名创建SequentialFile对象，这里只实现了MANIFEST文件的读写，针对log对象，没有实现，也就是没有考虑recover的情况
	virtual Status NewSequentialFile(const std::string& fname, SequentialFile** result) {//Y

		Status s;
		//printf("env_lds, NewSequentialFile, fname=%s\n", fname.c_str());
		if(fname.find(".ldb")!=-1){//this is ldb request.
			//printf("env_lds, NewSequentialFile, no implementation, exit(9)\n");
			exit(9);
			//LDS_Slot *slot=lds->alloc_slot(fname);

			//*result = new LDS_WritableSlot(fname, slot);
		}
		else if(fname.find("MANIFEST")!=-1){//this is manifest reqeust
			//new LDS_VersionLog;
			//printf("env_lds,NewSequentialFile, for manifest\n");
			
            //对MANIFEST文件进行读取
			//LDS_Log *manifest_ = lds->manifest;
            //根据文件名，首先创建一个LDS_log对象，分配buffer内存
			LDS_Log *manifest_ =lds->alloc_log(fname);

            //创建LDS_SequantialLog对象，用于顺序读取数据
			*result = new LDS_SequantialLog(fname, manifest_);
		}
		else if(fname.find(".log")!=-1){//this is backup log request
			//new LDS_BackupLog;
			//printf("env_lds,NewSequentialFile, for log\n");

		}
		else{//
			//printf("env_lds,NewSequentialFile, for other files\n");

			//direct other data to /dev/null
			LDS_Others* oth= new LDS_Others();
			*result = new LDS_SequentialOthers(fname, oth);
		}

		
		return s;

	}
	virtual Status NewRandomAccessFile(const std::string& fname, RandomAccessFile** result) {//用于sstable文件的读取
	
		Status s;
		//printf("env_lds, NewRandomAccessFile\n");
		if(fname.find(".ldb")!=-1){//this is ldb request.
				//exit(9);
            //根据文件名，创建一个slot对象，分配buffer内存
			LDS_Slot *slot =lds->alloc_slot(fname);
			
			uint64_t  size;
            //获取slot，也就是sstable文件的长度 
			size= read_chunk_size(slot);
			
			//printf("env_lds, NewRandomAccessFile,.ldb, file=%s size=%llu\n",fname.c_str(), size);
			//exit(0);
            //将slot在设备上的空间映射到虚拟地址空间
			void *base=mmap(NULL, size, PROT_READ, MAP_SHARED, slot->fd, slot->phy_offset);
			//创建一个LDS_MmapedSlot对象，用于对sstable进行随机存取
            *result = new LDS_MmapedSlot(fname, base, size);
			
		}
		else{
			//printf("env_lds, NewRandomAccessFile, no implementation, exit\n");
			exit(9);
		
		}

		return s;
	}

	virtual Status NewWritableFile(const std::string& fname,  WritableFile** result) {//用于对各个文件进行追加写操作
		Status s;

		//printf("env_lds, NewWritableFile, fname=%s\n",fname.c_str());
			
        ////针对sstable文件
		if(fname.find(".ldb")!=-1){//this is ldb request.
				//exit(9);
			LDS_Slot *slot=lds->alloc_slot(fname);

			*result = new LDS_WritableSlot(fname, slot);
		}
		else if(fname.find("MANIFEST")!=-1){//this is manifest reqeust
			//new LDS_VersionLog;
			//printf("env_lds,NewWritableFile, for manifest\n");
			//LDS_Log *manifest_ = lds->manifest;
			LDS_Log *manifest_ =lds->alloc_log(fname);
			*result = new LDS_WritableLog(fname, manifest_);
		}
		else if(fname.find(".log")!=-1){//this is backup log request
			//new LDS_BackupLog;
			//printf("env_lds,NewWritableFile, for log\n");

			LDS_Log *log_ =lds->alloc_log(fname);
			*result = new LDS_WritableLog(fname, log_);
			//exit(9);//to implement

		}
		else{//如果是其他文件
			//printf("env_lds,NewWritableFile, for other files\n");

			//direct other data to /dev/null
			FILE* nul=fopen("/dev/null","w");
			*result = new LDS_WritableOthers(fname, nul);
		}

		return s;

	}

    //判断文件是否存在
	virtual bool FileExists(const std::string& fname) {
		//printf("LDSEnv, FileExists, fname=%s\n", fname.c_str());
		if(fname.find("CURRENT")!=-1){//to see if the db exits.
			//we will check the beginning of the version log to decide if the db exits.
			//if db exits, return true
			//else return false
		}

		return false;//


	}


	virtual Status GetChildren(const std::string& name, std::vector<std::string>* result) {
		//printf("LDSEnv, GetChildren, name=%s\n", name.c_str());

		result->clear();
		return Status::OK();
   
	}

	virtual Status DeleteFile(const std::string& name) {
		//printf("LDSEnv, DeleteFile, name=%s\n", name.c_str());

		return Status::OK();
	}

	virtual Status CreateDir(const std::string& name) {
		//printf("LDSEnv, CreateDir, name=%s\n", name.c_str());
		return Status::OK();

	   
	}

	virtual Status DeleteDir(const std::string& name) {
		//printf("LDSEnv, DeleteDir, name=%s\n", name.c_str());
		return Status::OK();
	}

	virtual Status GetFileSize(const std::string& name, uint64_t* size) {
		//printf("LDSEnv, GetFileSize, name=%s\n", name.c_str());
		return Status::OK();
	}

	virtual Status RenameFile(const std::string& src, const std::string& target) {
		//printf("LDSEnv, RenameFile, src=%s, target=%s\n", src.c_str(),target.c_str());

	   return Status::OK();
	}

	virtual Status LockFile(const std::string& name, FileLock** lock) {
		//printf("LDSEnv, LockFile, name=%s\n", name.c_str());

		return Status::OK();
		
	}

	virtual Status UnlockFile(FileLock* lock) {
		//printf("LDSEnv, UnlockFile\n");
		return Status::OK();
	}

	virtual void Schedule(void (*function)(void*), void* arg);//线程调度

	virtual void StartThread(void (*function)(void* arg), void* arg);//线程分离

	virtual Status GetTestDirectory(std::string* result) {
    
	}

	virtual Status NewLogger(const std::string& fname, Logger** result) {
        std::FILE* fp = std::fopen(fname.c_str(), "w");
        if (fp == nullptr) {
          *result = nullptr;
          //printf("fp is nullptr\n");
          return Status::OK();
        } else {
          *result = new PosixLogger(fp);
          return Status::OK();
        }
		//return Status::OK();
	}

	virtual uint64_t NowMicros() {
        static constexpr uint64_t kUsecondsPerSecond = 1000000;
        struct ::timeval tv;
        ::gettimeofday(&tv, nullptr);
        return static_cast<uint64_t>(tv.tv_sec) * kUsecondsPerSecond + tv.tv_usec;
	}

	virtual void SleepForMicroseconds(int micros) {
		usleep(micros);
	}

 private:
	LDS *lds;//这个就是整个的接口
	void PthreadCall(const char* label, int result) {//线程调用
		if (result != 0) {
		  //fprintf(stderr, "pthread %s: %s\n", label, strerror(result));
		  abort();
		}
	}

  // BGThread() is the body of the background thread
  void BGThread();//实际用于处理的线程
  static void* BGThreadWrapper(void* arg) {//传递的静态函数
    reinterpret_cast<LDSEnv*>(arg)->BGThread();
    return NULL;
  }
  
  pthread_mutex_t mu_;//互斥锁
  pthread_cond_t bgsignal_;//条件变量
  pthread_t bgthread_;//后台线程
  bool started_bgthread_;//是否开启线程
  
   struct BGItem { void* arg; void (*function)(void*); };
  typedef std::deque<BGItem> BGQueue;
  BGQueue queue_;

 



};

struct StartThreadState {
  void (*user_function)(void*);
  void* arg;
};
static void* StartThreadWrapper(void* arg) {
  StartThreadState* state = reinterpret_cast<StartThreadState*>(arg);
  state->user_function(state->arg);
  delete state;
  return NULL;
}



//-----------------------------------------begin the LDSEnv:: functions-----------------------------------
//-----------------------------------------begin the LDSEnv:: functions-----------------------------------
//LDSEnv的构造函数
LDSEnv::LDSEnv() : started_bgthread_(false) {
    //默认开始不开启线程
	//printf("env_lds, LDSEnv is called, dev_name=%s\n",dev_name.c_str());
	//exit(9);
    //获取设备的路径名字
    //printf("LDSEnv create, dev_name:%s\n", dev_name.c_str());
	std::string path=dev_name;
    //获取LDS对象，flash_using_exist表示是否利用之前就存在的数据库，
	lds =new LDS(path, flash_using_exist); 
}

void LDSEnv::Schedule(void (*function)(void*), void* arg) {
	  PthreadCall("lock", pthread_mutex_lock(&mu_));//加锁
	//printf("env_lds.cc Schedule, begin,started_bgthread_=%d\n",started_bgthread_);
	  if (!started_bgthread_) {//创建线程，调用BGThread
		started_bgthread_ = true;
		PthreadCall(
			"create thread",
			pthread_create(&bgthread_, NULL,  &LDSEnv::BGThreadWrapper, this));
	  }
	  
	  if (queue_.empty()) {
		PthreadCall("signal", pthread_cond_signal(&bgsignal_));
	  }

      //将线程加入队列
	  // Add to priority queue
	  queue_.push_back(BGItem());
	  queue_.back().function = function;
	  queue_.back().arg = arg;

      //解锁
	  PthreadCall("unlock", pthread_mutex_unlock(&mu_));
}




void LDSEnv::StartThread(void (*function)(void* arg), void* arg) {//开启线程，分离
		 pthread_t t;
		StartThreadState* state = new StartThreadState;
		state->user_function = function;
		state->arg = arg;
		PthreadCall("start thread",
              pthread_create(&t, NULL,  &StartThreadWrapper, state));
}



void LDSEnv::BGThread() {

		//printf("env_lds.cc BGThread, begin\n");

  while (true) {
    // Wait until there is an item that is ready to run
    //加锁
    PthreadCall("lock", pthread_mutex_lock(&mu_));
    while (queue_.empty()) {
      PthreadCall("wait", pthread_cond_wait(&bgsignal_, &mu_));
    }

    ////队列不为空，有需要调度的线程
    void (*function)(void*) = queue_.front().function;
    void* arg = queue_.front().arg;
    queue_.pop_front();

    //解锁 
    PthreadCall("unlock", pthread_mutex_unlock(&mu_));
    (*function)(arg);//开启线程
  }
}
//-----------------------------------------end the LDSEnv:: functions-----------------------------------
//-----------------------------------------end the LDSEnv:: functions-----------------------------------


static pthread_once_t once = PTHREAD_ONCE_INIT;//保证在本进程中该函数只执行一次
static Env* default_env;
static void InitDefaultEnv() { default_env = new LDSEnv; }

Env* Env::Default() {//在options.cc文件options的默认构造函数中，env就是通过这个静态函数获取Env对象，现在对其进行更新
  pthread_once(&once, InitDefaultEnv);//创建了LDSEnv对象，以此调用lds接口
  return default_env;
}

}  // namespace leveldb
