#ifndef HASH_IO_CHANNEL_H__
#define HASH_IO_CHANNEL_H__
#include "emp-tool/utils/hash.h"
#include "emp-tool/io/net_io_channel.h"
/** @addtogroup IO
    @{
  */
namespace emp {  
class HashIO: public IOChannel<HashIO>{ public:
	Hash h;
	NetIO * netio;
	HashIO(NetIO * _netio ) {
		this->netio = _netio;
	}
	void send_data(const void * data, int len) {
		h.put(data, len);
	}
	void recv_data(void  * data, int len) {
		netio->recv_data(data, len);
		h.put(data, len);
	}
	void get_digest(char * dgst){
		h.digest(dgst);
	}
};
/**@}*/
}
#endif//HASH_IO_CHANNEL_H__
