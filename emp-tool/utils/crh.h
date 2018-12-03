#include "emp-tool/utils/prp.h"
#include <stdio.h>
#ifndef CRH_H__
#define CRH_H__
/** @addtogroup BP
  @{
 */
namespace emp {

class CRH: public PRP { 
public:
	CRH(const char * seed = fix_key):PRP(seed) {
	}

	CRH(const block& seed): PRP(seed) {
	}

	block H(block in) {
		block t = in; 
		permute_block(&t, 1);
		return xorBlocks(t, in);
	}
#pragma GCC push_options
#pragma GCC optimize ("unroll-loops")
	template<int n>
	void H(block out[n], block in[n]) {
		block tmp[n];
		for (int i = 0; i < n; ++i)
			tmp[i] = in[i];
		permute_block(tmp, n);
		xorBlocks_arr(out, in, tmp, n);
	}
#pragma GCC pop_options

	void Hn(block*out, block* in, int n, block * scratch = nullptr) {
		bool del = false;
		if(scratch == nullptr) {
			del = true;
			scratch = new block[n];
		} 
		for(int i = 0; i < n; ++i)
			scratch[i] = in[i];
		permute_block(scratch, n);
		xorBlocks_arr(out, in, scratch, n);
		if(del) {
			delete[] scratch;
			scratch = nullptr;
		}

	}
};
}
/**@}*/
#endif// CRH_H__
