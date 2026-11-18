#ifndef EMP_MITCCRH_H__
#define EMP_MITCCRH_H__
#include "emp-tool/runtime/crypto/aes.h"
#include <bit>
#include <cstdint>

namespace emp {

/*
 * [REF] Implementation of "Better Concrete Security for Half-Gates Garbling (in the Multi-Instance Setting)"
 * https://eprint.iacr.org/2019/1168.pdf
 *
 * The paper's hash is MMO^E(x, i) = E_{S ⊕ i}(σ(x)) ⊕ σ(x): a fresh AES key per
 * tweak i (the gate id). Scheduling a key per gid dominates its cost, so this
 * hash amortizes the schedule over a BUCKET of 2^ReuseShift consecutive gids
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
 * The tweak is a running gid, and the class owns it: setS(S) starts the stream
 * at 0 and every hash takes its gids from it. (S, gid) is the whole contract
 * between two parties — they must present the same pair for the same hash, and
 * a start point must never replay a gid. Tweak dimensions beyond the counter (a
 * lane, a party, a destination) are XORed into the low qword of a uniformly
 * random S — the key only ever adds makeBlock(i >> ReuseShift, 0) to it, and S
 * must stay random because the multi-instance bound rests on independent keys
 * across instances — or, with MITCCRHBulk, laid out as gid gaps with
 * renew_ks(gid); either way every hash still has a distinct (S, gid). Both
 * parties must use the same ReuseShift — it changes the keys, hence the
 * transcript.
 *
 * Two classes implement the hash. MITCCRH is a stream of single-use tweaks, the
 * way garbling and OT extension hash: gids are taken in order, each by exactly
 * one lane, and every hash runs under one key. MITCCRHBulk is the general form
 * for the rest: it can be repositioned, hash a run of gids that spans buckets,
 * or key every gid of a run separately. A gid hashes the same way in both, so
 * the two give the same bytes wherever they take the same gids.
 */

// A stream of single-use tweaks. setS(S) starts it at gid 0. A hash<K, H> or
// hash_cir<K, H> call of K lanes takes the next run of K gids that starts at a
// multiple of bit_ceil(K): lane k hashes its H blocks under gid g0 + k. The
// gids the alignment passes over are never hashed, so for K a power of two the
// stream is gapless, K gids per call. bit_ceil(K) must not exceed 2^ReuseShift,
// so every call lies in one bucket and runs as one wide tile under one key; a
// caller that needs more lanes per call than a bucket holds uses MITCCRHBulk.
//
// The stream only moves forward, so the keys it needs next are known in
// advance: when a call enters a bucket that is not scheduled yet, the keys of
// Ahead consecutive buckets from there are scheduled in one interleaved batch
// (AES_opt_key_schedule<Ahead>), which costs far less per key than scheduling
// them one at a time. Ahead changes only when keys are scheduled, never which
// key hashes which gid, so it is a per-machine choice that the other party
// need not match; it costs Ahead AES key schedules of state. Hence the order:
// ReuseShift, which both parties must match, first; Ahead last, with its
// default.
template<int ReuseShift = 3, int Ahead = 8>
class MITCCRH {
	static_assert(Ahead > 0, "MITCCRH: Ahead must be positive");
	static_assert(ReuseShift >= 0 && ReuseShift < 32, "MITCCRH: ReuseShift out of range");

	template<int K, int H>
	static constexpr void validate_hash_shape() {
		static_assert(K > 0, "MITCCRH: K must be positive");
		static_assert(H > 0, "MITCCRH: H must be positive");
		if constexpr (K > 0)
			static_assert(std::bit_ceil((unsigned)K) <= (1u << ReuseShift),
			              "MITCCRH: bit_ceil(K) must not exceed 2^ReuseShift (see MITCCRHBulk)");
	}

public:
	// Install the parties' shared random start point S and restart the stream
	// at gid 0.
	void setS(block sin) {
		start_point_ = sin;
		gid_ = 0;
		bucket0_ = ~0ull;   // no key is scheduled under the new S
	}

	// The start point S installed by setS().
	block start_point() const { return start_point_; }

	// The first gid no hash has taken: a call of K lanes starts at the next
	// multiple of bit_ceil(K) from here. 0 after setS(). For a stream that
	// hashes one power-of-two K, it is the number of gids hashed since setS().
	uint64_t next_gid() const { return gid_; }

	// On VAES builds all four entry points below read their input one block
	// at a time (AesLane::load_per_block). That suits input the caller has just
	// written block by block, as half-gate garbling does. It does not suit
	// chained calls: when the input was just written by full-width stores, such
	// as the previous call's output hashed again in place, the per-block loads
	// lengthen that chain and run slower than one wide load would.
	//
	// In-place: blks[k*H + h] = y ^ AES_{key}(y), y = sigma(blks[k*H + h]) ^ T(g0 + k).
	// sigma and the tweak are fused into the AES tile source
	// (AesMemXorSigmaTweakTile): the tweak vector is built in registers from
	// one broadcast of T(g0) plus compile-time lane offsets, and y stays live
	// in-register as the Davies-Meyer XOR-back operand.
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
	// Keys of buckets bucket0_ .. bucket0_ + Ahead - 1.
	AES_KEY scheduled_key_[Ahead];
	// Zero only so a fresh object is never indeterminate. An unseeded object is
	// not a keyed instance: setS() must install the parties' shared random S.
	block start_point_ = zero_block;
	// The first gid no hash has taken.
	uint64_t gid_ = 0;
	// First bucket scheduled_key_ covers; ~0 = none.
	uint64_t bucket0_ = ~0ull;

	// Shared body of hash / hash_cir: take the next bit_ceil(K)-aligned run of K
	// gids and hash its K*H blocks as one wide run under the run's bucket key;
	// the tile derives lane k's tweak from T(g0) and the block index (Span = H).
	// InPlace says dst and src are the same buffer, so the tile is handed one
	// pointer and keeps the exact aliasing knowledge (loads hoisted above
	// stores) of the in-place form; otherwise they must not overlap.
	template<class Tile, int K, int H, bool InPlace>
	void consume(block * dst, const block * src) {
		validate_hash_shape<K, H>();
		constexpr uint64_t align = std::bit_ceil((uint64_t)K);
		const uint64_t g0 = (gid_ + align - 1) & ~(align - 1);
		gid_ = g0 + K;
		const uint64_t bucket = g0 >> ReuseShift;
		if (bucket < bucket0_ || bucket - bucket0_ >= (uint64_t)Ahead) {
			alignas(16) block keys[Ahead];
			for (int i = 0; i < Ahead; ++i)
				keys[i] = start_point_ ^ makeBlock(bucket + (uint64_t)i, 0);
			AES_opt_key_schedule<Ahead>(keys, scheduled_key_);
			bucket0_ = bucket;
		}
		const block * const s = InPlace ? dst : src;
		const block tb = makeBlock(g0, 0);
		detail::ParaEnc_mem_tiles_impl<Tile, 1, K * H>(
			dst, s, scheduled_key_ + (bucket - bucket0_), &tb);
	}
};

// The general form of the same hash, for what MITCCRH does not do. It keys the
// gid stream in schedules of BatchSize consecutive gids: a hash<K, H> call (K
// dividing BatchSize) takes the next K lanes of the current schedule, or starts
// the next schedule when they do not fit, skipping the leftover lanes;
// renew_ks(gid) repositions the stream; and a schedule that spans buckets
// (2^ReuseShift < BatchSize, an unaligned gid, or ReuseShift = 0) keys each of
// its gids separately. For an instance that hashes one K, BatchSize changes
// only how many gids are keyed per refill, not the bytes.
template<int BatchSize = 8, int ReuseShift = 3>
class MITCCRHBulk {
	static_assert(BatchSize > 0, "MITCCRHBulk: BatchSize must be positive");

	template<int K, int H>
	static constexpr void validate_hash_shape() {
		static_assert(K > 0, "MITCCRHBulk: K must be positive");
		static_assert(H > 0, "MITCCRHBulk: H must be positive");
		if constexpr (K > 0) {
			static_assert(K <= BatchSize, "MITCCRHBulk: K must not exceed BatchSize");
			static_assert(BatchSize % K == 0, "MITCCRHBulk: K must divide BatchSize");
		}
	}

public:
	static_assert(ReuseShift >= 0 && ReuseShift < 32, "MITCCRHBulk: ReuseShift out of range");

	// Install the parties' shared random start point S and restart the gid
	// stream at 0.
	void setS(block sin) {
		start_point_ = sin;
		gid_ = 0;
		key_used_ = BatchSize;  // drop pre-scheduled keys; next hash re-keys under the new S
		scheduled_bucket_ = ~0ull;
		lane_gid0_ = 0;
	}

	// Reposition the gid stream: schedule from `gid`, so the next hash's first
	// lane hashes gid `gid`. A caller with a multi-dimensional tweak lays it
	// out as gid gaps (e.g. gid = gate * stride, one lane per destination) and
	// the class keeps every (S, gid) distinct, buckets and tweaks included.
	void renew_ks(uint64_t gid) {
		gid_ = gid;
		renew_ks();
	}

	// The start point S installed by setS().
	block start_point() const { return start_point_; }

	// The gid the next hash's first lane takes when its K lanes fit in the
	// current schedule; a hash whose lanes do not fit starts a new schedule
	// instead (see consume()). 0 after setS(). For an instance that always
	// hashes the same K, it is the number of gids hashed since setS().
	uint64_t next_gid() const {
		return key_used_ < BatchSize ? lane_gid0_ + (uint64_t)key_used_ : gid_;
	}

	// The entry points read their input as MITCCRH's do (one block at a time on
	// VAES builds) and compute the same blocks:
	// blks[k*H + h] = y ^ AES_{key(lane k)}(y), y = sigma(blks[k*H + h]) ^ T(gid of lane k)
	// for hash_cir, and y = blks[k*H + h] ^ T(gid of lane k) for hash.
	template<int K, int H>
	void hash_cir(block * blks) {
		consume<detail::AesMemXorSigmaTweakTile<H>, K, H, true>(blks, blks);
	}

	// Out-of-place: out and in must not overlap.
	template<int K, int H>
	void hash_cir(block * __restrict__ out, const block * __restrict__ in) {
		consume<detail::AesMemXorSigmaTweakTile<H>, K, H, false>(out, in);
	}

	template<int K, int H>
	void hash(block * blks) {
		consume<detail::AesMemXorInputTweakTile<H>, K, H, true>(blks, blks);
	}

	// Out-of-place: out and in must not overlap.
	template<int K, int H>
	void hash(block * __restrict__ out, const block * __restrict__ in) {
		consume<detail::AesMemXorInputTweakTile<H>, K, H, false>(out, in);
	}

private:
	AES_KEY scheduled_key_[BatchSize];
	block keys_[BatchSize];
	int key_used_ = BatchSize;
	// Zero only so a fresh object is never indeterminate. An unseeded object is
	// not a keyed instance: setS() must install the parties' shared random S.
	block start_point_ = zero_block;
	// First gid of the next schedule renew_ks() builds.
	uint64_t gid_ = 0;
	// Bucket the current schedule covers (ReuseShift > 0 only); ~0 = none.
	uint64_t scheduled_bucket_ = ~0ull;
	// gid hashed under scheduled_key_[0]: lane k of the current schedule hashes
	// gid lane_gid0_ + k, and T(that gid) enters its cipher input.
	uint64_t lane_gid0_ = 0;

	// Schedule BatchSize lanes starting at gid_.
	void renew_ks() {
		lane_gid0_ = gid_;
		if constexpr (ReuseShift == 0) {
			for(int i = 0; i < BatchSize; ++i)
				keys_[i] = start_point_ ^ makeBlock(gid_++, 0);
			AES_opt_key_schedule<BatchSize>(keys_, scheduled_key_);
		} else {
			uint64_t first = gid_ >> ReuseShift;
			uint64_t last  = (gid_ + BatchSize - 1) >> ReuseShift;
			if (first == last) {
				// Whole batch in one bucket (the aligned steady state): one
				// schedule into scheduled_key_[0] only — the hash detects the
				// uniform batch via scheduled_bucket_ and runs every block
				// under key 0, so no replication is needed. Skipped entirely
				// if the bucket is already scheduled (2^ReuseShift >
				// BatchSize spans renewals).
				if (first != scheduled_bucket_) {
					keys_[0] = start_point_ ^ makeBlock(first, 0);
					AES_opt_key_schedule<1>(keys_, scheduled_key_);
					scheduled_bucket_ = first;
				}
			} else {
				// Batch straddles buckets (unaligned gid, or
				// 2^ReuseShift < BatchSize): schedule per-gid bucket keys.
				for (int i = 0; i < BatchSize; ++i)
					keys_[i] = start_point_ ^ makeBlock((gid_ + i) >> ReuseShift, 0);
				AES_opt_key_schedule<BatchSize>(keys_, scheduled_key_);
				scheduled_bucket_ = ~0ull;   // mixed batch; don't cache
			}
			gid_ += BatchSize;
		}
		key_used_ = 0;
	}

	// Shared body of hash / hash_cir: consume the next K scheduled lanes, H
	// blocks each, through the fused tweak tile. Lane k hashes gid g0 + k,
	// g0 = lane_gid0_ + key_used_. A uniform bucket runs all K*H blocks under
	// key 0 as one wide run; its tile derives lane k's tweak from T(g0) and the
	// block index (Span = H). A straddling schedule runs lane k under
	// scheduled_key_[key_used_ + k] with T(g0 + k) as that key's base tweak.
	// InPlace says dst and src are the same buffer, so the tile is handed one
	// pointer and keeps the exact aliasing knowledge (loads hoisted above
	// stores) of the in-place form; otherwise they must not overlap.
	template<class Tile, int K, int H, bool InPlace>
	void consume(block * dst, const block * src) {
		validate_hash_shape<K, H>();
		// Renew when the next K lanes do not fit. With one K per schedule this
		// is exactly key_used_ == BatchSize; a caller that mixes K within a
		// schedule skips the leftover lanes instead of reading past the
		// schedule or hashing a gid under a neighbouring bucket's key, and a
		// skipped gid is never reused.
		if (key_used_ + K > BatchSize) renew_ks();
		const block * const s = InPlace ? dst : src;
		const uint64_t g0 = lane_gid0_ + (uint64_t)key_used_;
		if constexpr (ReuseShift > 0) {
			if (scheduled_bucket_ != ~0ull) {   // uniform batch: every key == key 0
				const block tb = makeBlock(g0, 0);
				detail::ParaEnc_mem_tiles_impl<Tile, 1, K * H>(dst, s, scheduled_key_, &tb);
				key_used_ += K;
				return;
			}
		}
		block tb[K];
		for (int k = 0; k < K; ++k) tb[k] = makeBlock(g0 + (uint64_t)k, 0);
		detail::ParaEnc_mem_tiles_impl<Tile, K, H>(
			dst, s, scheduled_key_ + key_used_, static_cast<const block *>(tb));
		key_used_ += K;
	}
};
}
#endif// MITCCRH_H__
