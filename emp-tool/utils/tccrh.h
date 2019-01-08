#include "emp-tool/utils/prp.h"
#include <stdio.h>
#ifndef TCCRH_H__
#define TCCRH_H__
/** @addtogroup BP
  @{
 */
namespace emp {

class TCCRH: public PRP { public:
	TCCRH(const char * seed = fix_key):PRP(seed) {
	}

	TCCRH(const block& seed): PRP(seed) {
	}
	block H(block in, uint64_t i) {
		permute_block(&in, 1);
		block t = xorBlocks(in, makeBlock(0, i));
		permute_block(&t, 1);
		return xorBlocks(t, in);
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
			out[i] = xorBlocks(tmp[i], makeBlock(0, id));
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
			out[i] = xorBlocks(scratch[i], makeBlock(0, id));
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
/**@}*/
#endif// TCCRH_H__
