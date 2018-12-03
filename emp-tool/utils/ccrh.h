#include "emp-tool/utils/prp.h"
#include <stdio.h>
#ifndef CCRH_H__
#define CCRH_H__
/** @addtogroup BP
  @{
 */
namespace emp {

class CCRH: public PRP { public:
	CCRH(const char * seed = fix_key):PRP(seed) {
	}

	CCRH(const block& seed): PRP(seed) {
	}

	block H(block in) {
		block t;
		t = in = xorBlocks(_mm_shuffle_epi32(in, 78), _mm_shuffle_epi32(in, 4)); 
		permute_block(&t, 1);
		return xorBlocks(t, in);
	}

#pragma GCC push_options
#pragma GCC optimize ("unroll-loops")
	template<int n>
	void H(block out[n], block in[n]) {
		block tmp[n];
		for (int i = 0; i < n; ++i)
			tmp[i] = out[i] = xorBlocks(_mm_shuffle_epi32(in[i], 78), _mm_shuffle_epi32(in[i], 4));
		permute_block(tmp, n);
		xorBlocks_arr(out, tmp, out, n);
	}
#pragma GCC pop_options

	void Hn(block*out, block* in, uint64_t id, int length, block * scratch = nullptr) {
		bool del = false;
		if(scratch == nullptr) {
			del = true;
			scratch = new block[length];
		}

		for (int i = 0; i < length; ++i)
			scratch[i] = out[i] = xorBlocks(_mm_shuffle_epi32(in[i], 78), _mm_shuffle_epi32(in[i], 4));
		permute_block(scratch, length);
		xorBlocks_arr(out, scratch, out, length);

		if(del) {
			delete[] scratch;
			scratch = nullptr;
		}
	}

};
}
/**@}*/
#endif// CCRH_H__
