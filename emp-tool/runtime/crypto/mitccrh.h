#ifndef EMP_MITCCRH_H__
#define EMP_MITCCRH_H__
#include "emp-tool/runtime/crypto/aes.h"
#include <stdio.h>

namespace emp {

/*
 * [REF] Implementation of "Better Concrete Security for Half-Gates Garbling (in the Multi-Instance Setting)"
 * https://eprint.iacr.org/2019/1168.pdf
 *
 * ReuseShift trades concrete security for key-schedule amortization WITHOUT
 * changing the call surface: the gid-derived AES key is taken from the BUCKET
 * gid >> ReuseShift, so 2^ReuseShift consecutive gids hash under one key and
 * the schedule runs once per bucket instead of once per gid. The multi-instance
 * bound degrades by ReuseShift bits (2^ReuseShift x the per-key data
 * complexity); the default ReuseShift = 3 costs 3 bits (125-bit at kappa = 128)
 * and amortizes the schedule 8x. ReuseShift = 0 selects the one-key-per-gid
 * construction. Both parties must use the same ReuseShift — it changes the
 * keys, hence the transcript. The explicit-tweak mode (renew_ks(tweaks)) is
 * NOT bucketed: callers that pass tweaks own their reuse policy.
 */

template<int BatchSize = 8, int ReuseShift = 3>
class MITCCRH {
	static_assert(BatchSize > 0, "MITCCRH: BatchSize must be positive");

	template<int K, int H>
	static consteval void validate_hash_shape() {
		static_assert(K > 0, "MITCCRH: K must be positive");
		static_assert(H > 0, "MITCCRH: H must be positive");
		if constexpr (K > 0) {
			static_assert(K <= BatchSize, "MITCCRH: K must not exceed BatchSize");
			static_assert(BatchSize % K == 0, "MITCCRH: K must divide BatchSize");
		}
	}

public:
	static_assert(ReuseShift >= 0 && ReuseShift < 32, "MITCCRH: ReuseShift out of range");
	AES_KEY scheduled_key[BatchSize];
	block keys[BatchSize];
	int key_used = BatchSize;
	block start_point = zero_block;
	uint64_t gid = 0;
	// Bucket the current schedule covers (ReuseShift > 0 only); ~0 = none.
	uint64_t scheduled_bucket = ~0ull;

	void setS(block sin) {
		this->start_point = sin;
		this->gid = 0;
		this->key_used = BatchSize;  // drop pre-scheduled keys; next hash re-keys under the new S
		this->scheduled_bucket = ~0ull;
	}

	void renew_ks(uint64_t gid) {
		this->gid = gid;
		renew_ks();
	}

	void renew_ks() {
		if constexpr (ReuseShift == 0) {
			for(int i = 0; i < BatchSize; ++i)
				keys[i] = start_point ^ makeBlock(gid++, 0);
			AES_opt_key_schedule<BatchSize>(keys, scheduled_key);
		} else {
			uint64_t first = gid >> ReuseShift;
			uint64_t last  = (gid + BatchSize - 1) >> ReuseShift;
			if (first == last) {
				// Whole batch in one bucket (the aligned steady state): one
				// schedule into scheduled_key[0] only — hash() detects the
				// uniform batch via scheduled_bucket and runs every block
				// under key 0, so no replication is needed. Skipped entirely
				// if the bucket is already scheduled (2^ReuseShift >
				// BatchSize spans renewals).
				if (first != scheduled_bucket) {
					keys[0] = start_point ^ makeBlock(first, 0);
					AES_opt_key_schedule<1>(keys, scheduled_key);
					scheduled_bucket = first;
				}
			} else {
				// Batch straddles buckets (unaligned gid, or
				// 2^ReuseShift < BatchSize): schedule per-gid bucket keys.
				for (int i = 0; i < BatchSize; ++i)
					keys[i] = start_point ^ makeBlock((gid + i) >> ReuseShift, 0);
				AES_opt_key_schedule<BatchSize>(keys, scheduled_key);
				scheduled_bucket = ~0ull;   // mixed batch; don't cache
			}
			gid += BatchSize;
		}
		key_used = 0;
	}

	// Schedule BatchSize keys from caller-supplied tweaks. Used when the
	// hash needs an explicit per-instance tweak rather than a sequential gid.
	// After this call, hash<K,H> consumes scheduled keys[0..K) — caller must
	// pick K so that K ≤ BatchSize and BatchSize % K == 0. ReuseShift does not
	// apply here (tweaks are opaque blocks; the caller owns reuse policy).
	//
	// Do NOT mix this with the gid-based renew_ks() on the same instance:
	// a tweak equal to makeBlock(g, 0) for some prior/future gid g would
	// produce a colliding AES key. Pick one mode per instance.
	void renew_ks(const block * tweaks) {
		for(int i = 0; i < BatchSize; ++i)
			keys[i] = start_point ^ tweaks[i];
		AES_opt_key_schedule<BatchSize>(keys, scheduled_key);
		key_used = 0;
		scheduled_bucket = ~0ull;   // schedule no longer gid-derived
	}

	// In-place: blks[i] = sigma(blks[i]) ^ AES(sigma(blks[i])). sigma is fused
	// into the AES tile source (AesMemXorSigmaTile, as CCRH does), so sigma(x)
	// stays live in-register as the Davies-Meyer XOR-back operand — no separate
	// sigma pass over memory.
	template<int K, int H>
	void hash_cir(block * blks) {
		validate_hash_shape<K, H>();
		if(key_used == BatchSize) renew_ks();
		if constexpr (ReuseShift > 0) {
			if (scheduled_bucket != ~0ull) {   // uniform batch: every key == key 0
				detail::ParaEnc_mem_tiles_impl<detail::AesMemXorSigmaTile, 1, K * H>(
					blks, blks, scheduled_key);
				key_used += K;
				return;
			}
		}
		detail::ParaEnc_mem_tiles_impl<detail::AesMemXorSigmaTile, K, H>(
			blks, blks, scheduled_key + key_used);
		key_used += K;
	}

	// Hash under the keys from the last renew_ks(tweaks) WITHOUT advancing to a new
	// key batch or re-scheduling. The caller owns the reuse policy: call
	// renew_ks(tweaks) once, then hash_cir_fixed<K,H>() for every instance that
	// should hash under those K keys -- e.g. a bucket of gates whose tweak differs
	// only below a reuse shift, giving the ReuseShift amortization to an EXPLICIT
	// multi-dimensional tweak the gid path cannot express. Uses keys[0..K) every
	// call; never touches key_used / gid / scheduled_bucket. Pair only with
	// renew_ks(tweaks), never the gid-based renew_ks().
	template<int K, int H>
	void hash_cir_fixed(block * blks) {
		validate_hash_shape<K, H>();
		detail::ParaEnc_mem_tiles_impl<detail::AesMemXorSigmaTile, K, H>(
			blks, blks, scheduled_key);
	}

	// Out-of-place: out[i] = sigma(in[i]) ^ AES(sigma(in[i])). out and in
	// must not overlap. Same fused-sigma tile; no scratch buffer.
	template<int K, int H>
	void hash_cir(block * __restrict__ out, const block * __restrict__ in) {
		validate_hash_shape<K, H>();
		if(key_used == BatchSize) renew_ks();
		if constexpr (ReuseShift > 0) {
			if (scheduled_bucket != ~0ull) {   // uniform batch: every key == key 0
				detail::ParaEnc_mem_tiles_impl<detail::AesMemXorSigmaTile, 1, K * H>(
					out, in, scheduled_key);
				key_used += K;
				return;
			}
		}
		detail::ParaEnc_mem_tiles_impl<detail::AesMemXorSigmaTile, K, H>(
			out, in, scheduled_key + key_used);
		key_used += K;
	}

	template<int K, int H>
	void hash(block * blks) {
		validate_hash_shape<K, H>();
		if(key_used == BatchSize) renew_ks();
		if constexpr (ReuseShift > 0) {
			if (scheduled_bucket != ~0ull) {   // uniform batch: every key == key 0
				detail::ParaEncXorInput_impl<1, K * H>(
					blks, blks, scheduled_key);
				key_used += K;
				return;
			}
		}
		detail::ParaEncXorInput_impl<K,H>(blks, blks, scheduled_key+key_used);
		key_used += K;
	}

	// Out-of-place: out[i] = in[i] ^ AES(in[i]). out and in must not overlap.
	// Feeds `in` directly through the two-pointer Davies-Meyer AES helper,
	// so neither form needs a stack copy or a second XOR-fold pass.
	template<int K, int H>
	void hash(block * __restrict__ out, const block * __restrict__ in) {
		validate_hash_shape<K, H>();
		if(key_used == BatchSize) renew_ks();
		if constexpr (ReuseShift > 0) {
			if (scheduled_bucket != ~0ull) {   // uniform batch: every key == key 0
				detail::ParaEncXorInput_impl<1, K * H>(
					out, in, scheduled_key);
				key_used += K;
				return;
			}
		}
		detail::ParaEncXorInput_impl<K,H>(out, in, scheduled_key+key_used);
		key_used += K;
	}

};
}
#endif// MITCCRH_H__
