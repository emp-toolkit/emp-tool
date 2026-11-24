#ifndef EMP_CCRH_H__
#define EMP_CCRH_H__
#include "emp-tool/runtime/crypto/prp.h"
#include <stdio.h>
namespace emp {

/*
 * Circular correlation-robust hash H_K(x) = σ(x) ⊕ AES_K(σ(x)), with AES_K
 * modeled as an ideal cipher. The key is public, and the caller must choose
 * it: a key that every instance shares makes H one fixed permutation for all
 * of them, so one evaluation can be tested against the secret inputs of every
 * instance at once, and the bound degrades with their total number (the
 * multi-instance setting of Guo-Katz-Wang-Weng-Yu, ePrint 2019/1168). Give each
 * instance its own key, for example one derived from its session id. MITCCRH
 * goes further and keys every tweak.
 */
class CCRH: public PRP { public:
	explicit CCRH(const block& key): PRP(key) {
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
