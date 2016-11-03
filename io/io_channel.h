#include "block.h"
#include "utils_ec.h"
#include "prg.h"
#ifndef IO_CHANNEL_H__
#define IO_CHANNEL_H__

/** @addtogroup IO
    @{
  */
  
template<typename T>
class IOChannel { public:
	PRG *prg = nullptr;
	void send_data(const void * data, int nbyte) {
		static_cast<T*>(this)->send_data_impl(data, nbyte);
	}
	void recv_data(void * data, int nbyte) {
		static_cast<T*>(this)->recv_data_impl(data, nbyte);
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

	void send_data_enc(const void * data, int len) {
		char * tmp = new char[len];
		prg->random_data(tmp, len);
		for(int i = 0; i < len; ++i)
			tmp[i] ^= ((char *)data)[i];
		send_data(tmp, len);
		delete[] tmp;
	}

	void send_block_enc(const block* data, int len) {
		block * tmp = new block[len];
		prg->random_block(tmp, len);
		for(int i = 0; i < len; ++i)
			tmp[i] = xorBlocks(data[i], tmp[i]);
		send_block(tmp, len);
		delete[] tmp;
	}
	void send_bn_enc(const bn_t * bn, size_t num) {
		uint64_t buffer[4];
		uint64_t buffer2[4];
		uint32_t bn_size;
		for(size_t i = 0; i < num; ++i) {
			bn_size = bn_size_raw(bn[i]);
			prg->random_data(buffer2, bn_size*sizeof(uint64_t));
			bn_write_raw(buffer, bn_size, bn[i]);
			for(size_t k = 0; k < bn_size; ++k)
				buffer[k]^=buffer2[k];
			send_data(&bn_size, sizeof(int));
			send_data(buffer, bn_size*sizeof(uint64_t));
		}
	}

	void recv_data_enc(void * data, int len) {
		recv_data(data, len);
		if(prg == nullptr)return;
		char * tmp = new char[len];
		prg->random_data(tmp, len);
		for(int i = 0; i < len; ++i)
			((char *)data)[i] ^= tmp[i];
		delete[] tmp;
	}
	void recv_block_enc(block* data, int len) {
		recv_block(data, len);
		if(prg == nullptr)return;
		block * tmp = new block[len];
		prg->random_block(tmp, len);
		for(int i = 0; i < len; ++i)
			data[i] = xorBlocks(data[i], tmp[i]);
		delete[] tmp;
	}
	void send_eb_enc(const eb_t * eb, size_t num) {
		uint8_t buffer[EB_SIZE];
		uint8_t buffer2[EB_SIZE];
		for(size_t i = 0; i < num; ++i) {
			uint32_t eb_size = eb_size_bin(eb[i], ECC_PACK);
			send_data(&eb_size, sizeof(int));
			prg->random_data(buffer2, eb_size);
			eb_write_bin(buffer, eb_size, eb[i], ECC_PACK);
			for(size_t k = 0; k < eb_size; ++k) {
				buffer[k] = (char)(buffer[k]^buffer2[k]);
			}
			send_data(buffer, eb_size*sizeof(uint8_t));
		}
	}

	void recv_eb_enc(eb_t* eb, size_t num) {
		uint8_t buffer[EB_SIZE];
		uint8_t buffer2[EB_SIZE];
		uint32_t eb_size;
		for(size_t i = 0; i < num; ++i) {
			recv_data(&eb_size, sizeof(int));
			recv_data(buffer, eb_size*sizeof(uint8_t));
			if(prg == nullptr)continue;
			prg->random_data(buffer2, eb_size);
			for(size_t k = 0; k < eb_size; ++k) {
				buffer[k] = (char)(buffer[k]^buffer2[k]);
			}
			eb_read_bin(eb[i], buffer, eb_size);
		}
	}
	void recv_bn_enc(bn_t* bn, size_t num) {
		uint64_t buffer[4];
		uint64_t buffer2[4];
		uint32_t bn_size;
		for(size_t i = 0; i < num; ++i) {
			recv_data(&bn_size, sizeof(int));
			recv_data(buffer, bn_size*sizeof(uint64_t));
			if(prg == nullptr)continue;
			prg->random_data(buffer2, sizeof(uint64_t)*bn_size);
			for(size_t k = 0; k < bn_size; ++k)
				buffer[k] ^=buffer2[k];
			bn_read_raw(bn[i], buffer, bn_size);
		}
	}

	void send_block(const block* data, int nblock) {
		send_data(data, nblock*sizeof(block));
	}

	void recv_block(block* data, int nblock) {
		recv_data(data, nblock*sizeof(block));
	}

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
};
/**@}*/
#endif// IO_CHANNEL_H__