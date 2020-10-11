#ifndef EMP_TCCRH_H__
#define EMP_TCCRH_H__
#include "emp-tool/utils/prp.h"
#include <stdio.h>

namespace emp {
/* 
 * By default, TCCRH use zero_block as the AES key.
 * Here we model f(x) = AES_{00..0}(x) as a random permutation (and thus in the RPM model)
 */

class TCCRH: public PRP { public:
	TCCRH(const block& key = zero_block): PRP(key) {
	}
	block H(block in, uint64_t i) {
		permute_block(&in, 1);
		block t = in ^ makeBlock(0, i);
		permute_block(&t, 1);
		return t ^ in;
	}

#ifdef __GNUC__
	#ifndef __clang__
		#pragma GCC push_options
		#pragma GCC optimize ("unroll-loops")
	#endif
#endif

	template<int n>
	void H(block out[n], block in[n], uint64_t id) {
		block tmp[n];
		for(int i = 0; i < n; ++i)
			tmp[i] = in[i];
		permute_block(tmp, n);
		for(int i = 0; i < n; ++i) {
			out[i] = tmp[i] ^ makeBlock(0, id);
			++id;
		}
		permute_block(out, n);
		xorBlocks_arr(out, tmp, out, n);
	}

#ifdef __GNUC__
	#ifndef __clang__
		#pragma GCC pop_options
	#endif
#endif


	void Hn(block*out, block* in, uint64_t id, int length, block * scratch = nullptr) {
		bool del = false;
		if(scratch == nullptr) {
			del = true;
			scratch = new block[length];
		} 
		for(int i = 0; i < length; ++i)
			scratch[i] = in[i];
		permute_block(scratch, length);
		for(int i = 0; i < length; ++i) {
			out[i] = scratch[i] ^ makeBlock(0, id);
			++id;
		}
		permute_block(out, length);
		xorBlocks_arr(out, scratch, out, length);

		if(del) {
			delete[] scratch;
			scratch = nullptr;
		}
	}

};
}
#endif// TCCRH_H__
