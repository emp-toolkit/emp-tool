#ifndef EMP_CCRH_H__
#define EMP_CCRH_H__
#include "emp-tool/runtime/crypto/prp.h"
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
		alignas(16) block t = sigma(in);
		detail::ParaEncXorInput_impl<1,1>(&t, &t, &aes);
		return t;
	}

	// H(x) = AES_K(σ(x)) ⊕ σ(x) (Davies–Meyer over σ). σ is folded into the
	// AES tile source, so σ(in) is never materialized to a buffer — it stays
	// live in-register as the XOR-back operand. For large n the fully-unrolled
	// body spills (each block needs a SIMD register); use Hn() for big batches.
	//
	// Out-of-place contract: `out` must equal `in` or not overlap it (the
	// fused forward pass would clobber a shifted view).
	template<int n>
	void H(block out[n], const block in[n]) {
		detail::ParaEnc_mem_tiles_impl<detail::AesMemXorSigmaTile, 1, n>(out, in, &aes);
	}

	// Runtime-sized, out-of-place: out and in must not overlap.
	void Hn(block * __restrict__ out, const block * __restrict__ in, int64_t length) {
		expecting(length >= 0, "CCRH::Hn: negative length");
		if (length == 0) return;
		detail::MemTileOp<detail::AesMemXorSigmaTile> op{out, in, &aes};
		detail::drain_tiles(op, length);
	}

	// Runtime-sized, in-place. Per-tile σ is computed in-register before the
	// store, so out == in is safe; a shifted overlap is not — copy first.
	void Hn(block *data, int64_t length) {
		expecting(length >= 0, "CCRH::Hn: negative length");
		if (length == 0) return;
		detail::MemTileOp<detail::AesMemXorSigmaTile> op{data, data, &aes};
		detail::drain_tiles(op, length);
	}
};

}//namespace
#endif// CCRH_H__
