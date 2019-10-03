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

	void send_pt(Group *G, const Point *A, int num_pts = 1) {
		for(int i = 0; i < num_pts; ++i) {
			size_t len = G.size_bin(A);
			unsigned char * data = new unsigned char[len];
			send_data(&len, 4);
			G->to_bin(data, A + i, len);
			send_data(data, len);
			delete [] data;
		}
	}

	void recv_pt(Group *G, Point *A, int num_pts = 1) {
		size_t len = 0;
		for(int i = 0; i < num_pts; ++i) {
			recv_data(&len, 4);
			unsigned char *data = new unsigned char[len];
			recv_data(data, len);
			G->from_bin(data, A + i, len);
			delete[] data;
		}
	}	

private:
	T& derived() {
		return *static_cast<T*>(this);
	}
};
/**@}*/
}
#endif// IO_CHANNEL_H__