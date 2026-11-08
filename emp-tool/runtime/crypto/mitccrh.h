#ifndef EMP_MITCCRH_H__
#define EMP_MITCCRH_H__
#include "emp-tool/runtime/crypto/aes.h"

namespace emp {

/*
 * [REF] Implementation of "Better Concrete Security for Half-Gates Garbling (in the Multi-Instance Setting)"
 * https://eprint.iacr.org/2019/1168.pdf
 *
 * The paper's hash is MMO^E(x, i) = E_{S ⊕ i}(σ(x)) ⊕ σ(x): a fresh AES key per
 * tweak i (the gate id). Scheduling a key per gid dominates its cost, so this
 * class amortizes the schedule over a BUCKET of 2^ReuseShift consecutive gids
 * that share the key S ⊕ (i >> ReuseShift), and moves the part of the tweak
 * the key does not carry into the cipher input:
 *
 *   H(x, i) = y ⊕ E_{S ⊕ (i >> ReuseShift)}(y),  y = σ(x) ⊕ T(i),  T(i) = makeBlock(i, 0)
 *
 * (hash_cir; hash omits σ). That is the paper's construction on the shifted
 * input x ⊕ σ^{-1}(T(i)) under tweak i >> ReuseShift, so its bound carries
 * over with the per-tweak multiplicity scaled by 2^ReuseShift: the default
 * ReuseShift = 3 costs 3 bits of the multi-instance bound and runs the
 * schedule once per 8 gids. ReuseShift = 0 keys every gid separately; T(i)
 * is applied there too, so there is one construction and one reference.
 *
 * The input tweak is not optional once keys are shared. Without it two gids
 * of a bucket hash a block to the same value, and a garbling caller that
 * hashes one wire label under two gates of a bucket (a wire fanning out into
 * two ANDs, or AND(a, a)) publishes rows whose XOR is 0 or Δ, handing the
 * evaluator Δ. T(i) puts every (label, gid) pair at its own cipher input; two
 * labels x, x' collide under one key only if σ(x) ⊕ σ(x') = T(i) ⊕ T(i'),
 * i.e. they differ by exactly makeBlock(0, i ⊕ i') (σ^{-1} moves the gid to
 * the low qword) — for κ-bit random labels a 2^-κ event per pair, of the kind
 * the paper's bad-event analysis already charges.
 *
 * The tweak is the running gid, and the class owns it: setS(S) starts the
 * stream at 0, each hash<K, H> / hash_cir<K, H> call consumes K consecutive
 * gids (one per lane of H blocks), and renew_ks(gid) repositions the stream.
 * (S, gid) is the whole contract between two parties — they must present the
 * same pair for the same hash, and a start point must never replay a gid.
 * Tweak dimensions beyond the counter (a lane, a party, a destination) are
 * XORed into the low qword of a uniformly random S — the key only ever adds
 * makeBlock(i >> ReuseShift, 0) to it, and S must stay random because the
 * multi-instance bound rests on independent keys across instances — or laid
 * out as gid gaps with renew_ks(gid); either way every hash still has a
 * distinct (S, gid). Both parties must use the same ReuseShift — it changes
 * the keys, hence the transcript.
 */

template<int BatchSize = 8, int ReuseShift = 3>
class MITCCRH {
	static_assert(BatchSize > 0, "MITCCRH: BatchSize must be positive");

	template<int K, int H>
	static constexpr void validate_hash_shape() {
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
	// Zero only so a fresh object is never indeterminate. An unseeded object is
	// not a keyed instance: setS() must install the parties' shared random S.
	block start_point = zero_block;
	uint64_t gid = 0;
	// Bucket the current schedule covers (ReuseShift > 0 only); ~0 = none.
	uint64_t scheduled_bucket = ~0ull;
	// gid hashed under scheduled_key[0]: lane k of the current schedule hashes
	// gid lane_gid0 + k, and T(that gid) enters its cipher input.
	uint64_t lane_gid0 = 0;

	void setS(block sin) {
		this->start_point = sin;
		this->gid = 0;
		this->key_used = BatchSize;  // drop pre-scheduled keys; next hash re-keys under the new S
		this->scheduled_bucket = ~0ull;
		this->lane_gid0 = 0;
	}

	// Reposition the gid stream: schedule from `gid`, so the next hash's first
	// lane hashes gid `gid`. A caller with a multi-dimensional tweak lays it
	// out as gid gaps (e.g. gid = gate * stride, one lane per destination) and
	// the class keeps every (S, gid) distinct, buckets and tweaks included.
	void renew_ks(uint64_t gid) {
		this->gid = gid;
		renew_ks();
	}

	void renew_ks() {
		lane_gid0 = gid;
		if constexpr (ReuseShift == 0) {
			for(int i = 0; i < BatchSize; ++i)
				keys[i] = start_point ^ makeBlock(gid++, 0);
			AES_opt_key_schedule<BatchSize>(keys, scheduled_key);
		} else {
			uint64_t first = gid >> ReuseShift;
			uint64_t last  = (gid + BatchSize - 1) >> ReuseShift;
			if (first == last) {
				// Whole batch in one bucket (the aligned steady state): one
				// schedule into scheduled_key[0] only — the hash detects the
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

	// On VAES builds all four entry points below read their input one block
	// at a time (AesLane::load_per_block). That suits input the caller has just
	// written block by block, as half-gate garbling does. It does not suit
	// chained calls: when the input was just written by full-width stores, such
	// as the previous call's output hashed again in place, the per-block loads
	// lengthen that chain and run slower than one wide load would.
	//
	// In-place: blks[k*H + h] = y ^ AES_{key(lane k)}(y), y = sigma(blks[k*H + h]) ^ T(gid of lane k).
	// sigma and the tweak are fused into the AES tile source
	// (AesMemXorSigmaTweakTile): the tweak vector is built in registers from
	// one broadcast of the first lane's T per key plus compile-time lane
	// offsets, and y stays live in-register as the Davies-Meyer XOR-back
	// operand.
	template<int K, int H>
	void hash_cir(block * blks) {
		consume<detail::AesMemXorSigmaTweakTile<H>, K, H, true>(blks, blks);
	}

	// Out-of-place: out[i] = y ^ AES(y), y = sigma(in[i]) ^ T. out and in
	// must not overlap. Same fused tile.
	template<int K, int H>
	void hash_cir(block * __restrict__ out, const block * __restrict__ in) {
		consume<detail::AesMemXorSigmaTweakTile<H>, K, H, false>(out, in);
	}

	// No-sigma form: blks[i] = y ^ AES(y), y = blks[i] ^ T(gid of its lane).
	template<int K, int H>
	void hash(block * blks) {
		consume<detail::AesMemXorInputTweakTile<H>, K, H, true>(blks, blks);
	}

	// Out-of-place: out[i] = y ^ AES(y), y = in[i] ^ T. out and in must not
	// overlap. Feeds `in` straight through the fused tile.
	template<int K, int H>
	void hash(block * __restrict__ out, const block * __restrict__ in) {
		consume<detail::AesMemXorInputTweakTile<H>, K, H, false>(out, in);
	}

private:
	// Shared body of hash / hash_cir: consume the next K scheduled lanes, H
	// blocks each, through the fused tweak tile. Lane k hashes gid g0 + k,
	// g0 = lane_gid0 + key_used. A uniform bucket runs all K*H blocks under
	// key 0 as one wide run; its tile derives lane k's tweak from T(g0) and the
	// block index (Span = H). A straddling schedule runs lane k under
	// scheduled_key[key_used + k] with T(g0 + k) as that key's base tweak.
	// InPlace says dst and src are the same buffer, so the tile is handed one
	// pointer and keeps the exact aliasing knowledge (loads hoisted above
	// stores) of the in-place form; otherwise they must not overlap.
	template<class Tile, int K, int H, bool InPlace>
	void consume(block * dst, const block * src) {
		validate_hash_shape<K, H>();
		// Renew when the next K lanes do not fit. With one K per schedule this
		// is exactly key_used == BatchSize; a caller that mixes K within a
		// schedule skips the leftover lanes instead of reading past the
		// schedule or hashing a gid under a neighbouring bucket's key, and a
		// skipped gid is never reused.
		if (key_used + K > BatchSize) renew_ks();
		const block * const s = InPlace ? dst : src;
		const uint64_t g0 = lane_gid0 + (uint64_t)key_used;
		if constexpr (ReuseShift > 0) {
			if (scheduled_bucket != ~0ull) {   // uniform batch: every key == key 0
				const block tb = makeBlock(g0, 0);
				detail::ParaEnc_mem_tiles_impl<Tile, 1, K * H>(dst, s, scheduled_key, &tb);
				key_used += K;
				return;
			}
		}
		block tb[K];
		for (int k = 0; k < K; ++k) tb[k] = makeBlock(g0 + (uint64_t)k, 0);
		detail::ParaEnc_mem_tiles_impl<Tile, K, H>(
			dst, s, scheduled_key + key_used, static_cast<const block *>(tb));
		key_used += K;
	}
};
}
#endif// MITCCRH_H__
