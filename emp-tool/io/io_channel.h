#ifndef IO_CHANNEL_H__
#define IO_CHANNEL_H__
#include "emp-tool/utils/block.h"
#include "emp-tool/utils/prg.h"
#include "emp-tool/utils/group.h"

/** @addtogroup IO
  @{
 */

namespace emp {
template<typename T>
class IOChannel {
public:
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

	void send_pt(Point *A, int num_pts = 1) {
		for(int i = 0; i < num_pts; ++i) {
			size_t len = A[i].size();
			A[i].group->resize_scratch(len);
			unsigned char * tmp = A[i].group->scratch;
			send_data(&len, 4);
			A[i].to_bin(tmp, len);
			send_data(tmp, len);
		}
	}

	void recv_pt(Group * g, Point *A, int num_pts = 1) {
		size_t len = 0;
		for(int i = 0; i < num_pts; ++i) {
			recv_data(&len, 4);
			g->resize_scratch(len);
			unsigned char * tmp = g->scratch;
			recv_data(tmp, len);
			A[i].from_bin(g, tmp, len);
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