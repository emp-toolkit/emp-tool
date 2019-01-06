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
		t = in = sigma(in);
		permute_block(&t, 1);
		return xorBlocks(t, in);
	}

	static block sigma(block a) {
		return xorBlocks(_mm_shuffle_epi32(a, 78), _mm_and_si128(a, _mm_set_epi64x(0xFFFFFFFFFFFFFFFF, 0x00)));
	}

#ifdef __GNUC__
	#ifndef __clang__
		#pragma GCC push_options
		#pragma GCC optimize ("unroll-loops")
	#endif
#endif

	template<int n>
	void H(block out[n], block in[n]) {
		block tmp[n];
		for (int i = 0; i < n; ++i)
			tmp[i] = out[i] = sigma(in[i]);
		permute_block(tmp, n);
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

		for (int i = 0; i < length; ++i)
			scratch[i] = out[i] = sigma(in[i]);
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
