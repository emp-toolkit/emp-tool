#ifndef EMP_CCRH_H__
#define EMP_CCRH_H__
#include "emp-tool/crypto/prp.h"
#include <stdio.h>
namespace emp {

/*
 * By default, CCRH uses zero_block as the AES key.
 * Here we model f(x) = AES_{00..0}(x) as a random permutation (and thus in the RPM model)
 */
class CCRH: public PRP { public:
	CCRH(const block& key = zero_block): PRP(key) {
	}

	block H(block in) {
		block t;
		t = in = sigma(in);
		permute_block(&t, 1);
		return t ^ in;
	}

	// NOTE: the body is fully unrolled by the compiler for small n. For
	// n ≳ 64 the unrolled body spills its per-block scratch (each block
	// needs a SIMD register; the architectural file holds 16–32 of them
	// depending on the target). Callers wanting large batches should use
	// Hn() below, or stay at n ≤ 16 where throughput peaks.
	//
	// H(x) = AES_K(σ(x)) ⊕ σ(x) (Davies–Meyer over σ). Compute σ(in)
	// once into `pt`, AES it into `out` out-of-place, then XOR pt back
	// — one σ pass instead of two and `out` is touched twice (AES write
	// + XOR-back), not three times (σ write + AES read/write + XOR).
	template<int n>
	void H(block out[n], block in[n]) {
		block pt[n];
		for (int i = 0; i < n; ++i) pt[i] = sigma(in[i]);
		ParaEnc<1, n>(out, pt, &aes);
		xorBlocksTo_arr(out, pt, n);
	}

	void Hn(block*out, block* in, int64_t length, block * scratch = nullptr) {
		if (length <= 0) return;
		bool del = false;
		if(scratch == nullptr) {
			del = true;
			scratch = new block[length];
		}

		for (int64_t i = 0; i < length; ++i)
			scratch[i] = out[i] = sigma(in[i]);

		permute_block(scratch, length);
		xorBlocksTo_arr(out, scratch, length);

		if(del) {
			delete[] scratch;
			scratch = nullptr;
		}
	}
};

}//namespace
#endif// CCRH_H__
