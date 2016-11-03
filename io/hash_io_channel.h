#ifndef HASH_IO_CHANNEL_H__
#define HASH_IO_CHANNEL_H__
#include "hash.h"
#include "net_io_channel.h"
/** @addtogroup IO
    @{
  */
  
class HashIO: public IOChannel<HashIO>{ public:
	Hash h;
	NetIO * netio;
	HashIO(NetIO * _netio ) {
		this->netio = _netio;
	}
	void send_data_impl(const void * data, int len) {
		h.put(data, len);
	}
	void recv_data_impl(void  * data, int len) {
		netio->recv_data(data, len);
		h.put(data, len);
	}
	void get_digest(char * dgst){
		h.digest(dgst);
	}
};
/**@}*/
#endif//HASH_IO_CHANNEL_H__
