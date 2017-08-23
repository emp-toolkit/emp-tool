#ifndef CHECK_IO_CHANNEL_H__
#define CHECK_IO_CHANNEL_H__
#include <iostream>
#include <string>
#include "emp-tool/io/net_io_channel.h"
/** @addtogroup IO
    @{
  */
  
namespace emp {
class CheckIO: public IOChannel<CheckIO> { public:
	NetIO * netio;
	bool check_result = true;
	char * buffer = nullptr;
	char * net_buffer = nullptr;
	int check_size = 0;
	CheckIO(NetIO * netio ) {
		this->netio = netio;
		buffer = new char[CHECK_BUFFER_SIZE];
		net_buffer = new char[CHECK_BUFFER_SIZE];
	}
	~CheckIO(){
		delete[] buffer;
		delete[] net_buffer;
	}

	bool get_check_result() {
		netio->recv_data(net_buffer, check_size);
		check_result &= (strncmp(net_buffer, buffer, check_size)==0);
		check_size = 0;
		return check_result;
	}


	void recv_data(void  * data, int len) {
	}
	void send_data(const void * data, int len) {
		assert(len < CHECK_BUFFER_SIZE);
		if(check_size + len >= CHECK_BUFFER_SIZE) {
			netio->recv_data(net_buffer, check_size);
			check_result &= (strncmp(net_buffer, buffer, check_size)==0);
			check_size = 0;
		}
		memcpy(buffer+check_size, data, len);
		check_size += len;
	}
};
}
/**@}*/
#endif//CHECK_IO_CHANNEL
