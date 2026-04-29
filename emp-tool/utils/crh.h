#ifndef EMP_CRH_H__
#define EMP_CRH_H__
#include "emp-tool/utils/prp.h"
#include <stdio.h>

namespace emp {

/*
 * By default, CRH uses zero_block as the AES key.
 * Here we model f(x) = AES_{00..0}(x) as a random permutation (and thus in the RPM model)
 */
class CRH: public PRP { public:
	CRH(const block& key = zero_block): PRP(key) {
	}

	block H(block in) {
		block t = in; 
		permute_block(&t, 1);
		return t ^ in;
	}

	// NOTE: see ccrh.h. Same spilling caveat at large n; prefer Hn() or
	// keep n ≤ 16.
	template<int n>
		void H(block out[n], block in[n]) {
			block tmp[n];
			for (int i = 0; i < n; ++i)
				tmp[i] = in[i];
			permute_block(tmp, n);
			// Dispatch keeps both kernels fully __restrict__-licensed
			// instead of relying on the compiler's auto-versioning.
			if (out == in)
				xorBlocksTo_arr(out, tmp, n);
			else
				xorBlocks_arr(out, in, tmp, n);
		}

	void Hn(block*out, block* in, int n, block * scratch = nullptr) {
		bool del = false;
		if(scratch == nullptr) {
			del = true;
			scratch = new block[n];
		} 
		for(int i = 0; i < n; ++i)
			scratch[i] = in[i];
		permute_block(scratch, n);
		if (out == in)
			xorBlocksTo_arr(out, scratch, n);
		else
			xorBlocks_arr(out, in, scratch, n);
		if(del) {
			delete[] scratch;
			scratch = nullptr;
		}
	}
};
}
#endif// CRH_H__
