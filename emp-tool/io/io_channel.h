#ifndef EMP_IO_CHANNEL_H__
#define EMP_IO_CHANNEL_H__
#include "emp-tool/utils/block.h"
#include "emp-tool/utils/prg.h"
#include "emp-tool/utils/group.h"
#include <memory>
#include <cassert>  

namespace emp {
template<typename T> 
class IOChannel { public:
	uint64_t counter = 0;
	void send_data(const void * data, size_t nbyte) {
		counter +=nbyte;
		derived().send_data_internal(data, nbyte);
	}

	void recv_data(void * data, size_t nbyte) {
		derived().recv_data_internal(data, nbyte);
	}

	void send_block(const block* data, size_t nblock) {
		send_data(data, nblock*sizeof(block));
	}

	void recv_block(block* data, size_t nblock) {
		recv_data(data, nblock*sizeof(block));
	}

	void send_pt(Point *A, size_t num_pts = 1) {
		for(size_t i = 0; i < num_pts; ++i) {
			size_t len = A[i].size();
			A[i].group->resize_scratch(len);
			unsigned char * tmp = A[i].group->scratch;
			send_data(&len, 4);
			A[i].to_bin(tmp, len);
			send_data(tmp, len);
		}
	}

	void recv_pt(Group * g, Point *A, size_t num_pts = 1) {
		size_t len = 0;
		for(size_t i = 0; i < num_pts; ++i) {
			recv_data(&len, 4);
			assert(len <= 2048);
			g->resize_scratch(len);
			unsigned char * tmp = g->scratch;
			recv_data(tmp, len);
			A[i].from_bin(g, tmp, len);
		}
	}	

	void send_bool(bool * data, size_t length) {
		void * ptr = (void *)data;
		size_t space = length;
		const void * aligned = std::align(alignof(uint64_t), sizeof(uint64_t), ptr, space);
		if(aligned == nullptr)
			send_data(data, length);
		else{
			size_t diff = length - space;
			send_data(data, diff);
			send_bool_aligned((const bool*)aligned, length - diff);
		}
	}

	void recv_bool(bool * data, size_t length) {
		void * ptr = (void *)data;
		size_t space = length;
		void * aligned = std::align(alignof(uint64_t), sizeof(uint64_t), ptr, space);
		if(aligned == nullptr)
			recv_data(data, length);
		else{
			size_t diff = length - space;
			recv_data(data, diff);
			recv_bool_aligned((bool*)aligned, length - diff);
		}
	}


	void send_bool_aligned(const bool * data, size_t length) {
		const bool * data64 = data;
		size_t i = 0;
                unsigned long long unpack;
		for(; i < length/8; ++i) {
			unsigned long long mask = 0x0101010101010101ULL;
			unsigned long long tmp = 0;
                        memcpy(&unpack, data64, sizeof(unpack));
                        data64 += sizeof(unpack);
#if defined(__BMI2__)
			tmp = _pext_u64(unpack, mask);
#else
			// https://github.com/Forceflow/libmorton/issues/6
			for (unsigned long long bb = 1; mask != 0; bb += bb) {
				if (unpack & mask & -mask) { tmp |= bb; }
				mask &= (mask - 1);
			}
#endif
			send_data(&tmp, 1);
		}
		if (8*i != length)
			send_data(data + 8*i, length - 8*i);
	}
	void recv_bool_aligned(bool * data, size_t length) {
		bool * data64 = data;
		size_t i = 0;
                unsigned long long unpack;
		for(; i < length/8; ++i) {
			unsigned long long mask = 0x0101010101010101ULL;
			unsigned long long tmp = 0;
			recv_data(&tmp, 1);
#if defined(__BMI2__)
			unpack = _pdep_u64(tmp, mask);
#else
			unpack = 0;
			for (unsigned long long bb = 1; mask != 0; bb += bb) {
				if (tmp & bb) {unpack |= mask & (-mask); }
				mask &= (mask - 1);
			}
#endif
                        memcpy(data64, &unpack, sizeof(unpack));
                        data64 += sizeof(unpack);
                        
		}
		if (8*i != length)
			recv_data(data + 8*i, length - 8*i);
	}


	private:
	T& derived() {
		return *static_cast<T*>(this);
	}
};
}
#endif
