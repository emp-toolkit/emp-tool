#ifndef IO_CHANNEL_H__
#define IO_CHANNEL_H__
#include "emp-tool/utils/block.h"
#include "emp-tool/utils/prg.h"


/** @addtogroup IO
  @{
 */

namespace emp {
template<typename T>
class IOChannel {
public:
	PRG *prg = nullptr;
	void send_data(const void * data, int nbyte) {
		derived().send_data(data, nbyte);
	}
	void recv_data(void * data, int nbyte) {
		derived().recv_data(data, nbyte);
	}

	void send_block(const block* data, int nblock) {
		send_data(data, nblock*sizeof(block));
	}

	void recv_block(block* data, int nblock) {
		recv_data(data, nblock*sizeof(block));
	}

	~IOChannel() {
		if(prg != nullptr)
			delete prg;
	}

	void set_key(const block* b) {
		if(b == nullptr) {
			delete prg;
			prg = nullptr;
			return;
		}
		if(prg != nullptr) 
			delete prg;
		prg = new PRG(b);
	}
/*TODO
	void send_eb(const eb_t * eb, size_t num) {
		uint8_t buffer[EB_SIZE];
		for(size_t i = 0; i < num; ++i) {
			int eb_size = eb_size_bin(eb[i], ECC_PACK);
			eb_write_bin(buffer, eb_size, eb[i], ECC_PACK);
			send_data(&eb_size, sizeof(int));
			send_data(buffer, eb_size*sizeof(uint8_t));
		}
	}

	void recv_eb(eb_t* eb, size_t num) {
		uint8_t buffer[EB_SIZE];
		int eb_size;
		for(size_t i = 0; i < num; ++i) {
			recv_data(&eb_size, sizeof(int));
			recv_data(buffer, eb_size*sizeof(uint8_t));
			eb_read_bin(eb[i], buffer, eb_size);
		}
	}

	void send_bn(const bn_t * bn, size_t num) {
		uint64_t buffer[4];
		int bn_size;
		for(size_t i = 0; i < num; ++i) {
			bn_size = bn_size_raw(bn[i]);	
			bn_write_raw(buffer, bn_size, bn[i]);
			send_data(&bn_size, sizeof(int));
			send_data(buffer, bn_size*sizeof(uint64_t));
		}
	}

	void recv_bn(bn_t* bn, size_t num) {
		uint64_t buffer[4];
		int bn_size;
		for(size_t i = 0; i < num; ++i) {
			recv_data(&bn_size, sizeof(int));
			recv_data(buffer, bn_size*sizeof(uint64_t));
			bn_read_raw(bn[i], buffer, bn_size);
		}
	}
	*/
	void send_pt(const Group &G,const Point &A)
	{
		char *data = G.to_hex(A);
		int len = strlen(data);
		send_data(&len, 4);
		send_data(data, len);
	}

	void recv_pt(const Group &G,Point &A)
	{

		int len;
		char *data;
		recv_data(&len, 4);
		data = new char[len + 1];
		data[len] = 0;
		recv_data(data, len);
		G.from_hex(A, data);
	}	

private:
	T& derived() {
		return *static_cast<T*>(this);
	}
};
/**@}*/
}
#endif// IO_CHANNEL_H__
