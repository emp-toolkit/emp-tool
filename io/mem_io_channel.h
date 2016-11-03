#include <string>
#include "io_channel.h"
#include "file_io_channel.h"

#ifndef MEM_IO_CHANNEL
#define MEM_IO_CHANNEL

/** @addtogroup IO
    @{
  */
  
class MemIO: public IOChannel<MemIO> { public:
	char * buffer = nullptr;
	int size = 0;
	int read_pos = 0;
	int cap;

	MemIO(int _cap=1024*1024) {
		this->cap = _cap;
		buffer = new char[cap];
		size = 0;
	}
	~MemIO(){
		if(buffer!=nullptr)
			delete[] buffer;
	}
	void load_from_file(FileIO * fio, uint64_t size) {
		delete[] buffer;
		buffer = new char[size];
		this->cap = size;
		this->read_pos = 0;
		this->size = size;
		fio->recv_data(buffer, size);
	}
	void clear(){
		size = 0;
	}
	void send_data_impl(const void * data, int len) {
		if(size + len >= cap){
			char * new_buffer = new char[2*(cap+len)];
			memcpy(new_buffer, buffer, size);
			delete[] buffer;
			buffer = new_buffer;
			cap = 2*(cap + len);
		}
		memcpy(buffer + size, data, len);
		size += len;
	}

	void recv_data_impl(void  * data, int len) {
		if(read_pos + len <= size) {
			memcpy(data, buffer + read_pos, len);
			read_pos += len;
		} else {
			fprintf(stderr,"error: mem_recv_data\n");
		}
	}
};
/**@}*/
#endif//MEM_IO_CHANNEL_H__
