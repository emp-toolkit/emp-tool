// crypto/mitccrh.h — Multi-Instance Tweakable CCRH (Guo–Katz–Wang–Weng–Yu,
// eprint 2019/1168). A bucketed-key Davies-Meyer AES hash used by half-gates
// garbling. Read example() first; the rest is verification.
//
// What's in mitccrh.h:
//   MITCCRH<ReuseShift, Ahead>               a stream of single-use tweaks; start_point(),
//                                            next_gid()
//   setS(s)                                  install S, restart the gid stream at 0
//   hash<K, H>(blks)                         take the next bit_ceil(K)-aligned run of K
//                                            gids, hash K*H blocks under its bucket key:
//                                              y ^ AES_{S ^ (gid>>RS)}(y), y = x ^ T(gid)
//   hash_cir<K, H>(blks)                     same with y = sigma(x) ^ T(gid)
//   MITCCRHBulk<BatchSize, ReuseShift>       the general form: start_point(), next_gid(),
//                                            renew_ks(gid), runs that span buckets
// T(gid) = makeBlock(gid, 0): the gid enters the cipher input, so gids that
// share one bucket key (RS > 0) still hash the same block to distinct values.

#include "emp-tool/emp-tool.h"

#include <bit>
#include <chrono>
#include <cstdint>
#include <type_traits>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace emp;
using namespace std;

// ---------- example ----------

static void example() {
	cout << "=== example ===\n";

	// (1) setS + hash<K, H>: feed K*H blocks; lane k hashes under the key of
	// bucket gid_k >> ReuseShift, with T(gid_k) XORed into the cipher input
	// and kept in the Davies-Meyer XOR-back.
	const block S = makeBlock(0xdeadbeefULL, 0xcafebabeULL);
	MITCCRH<> mit;
	mit.setS(S);

	block buf[8] = {
		makeBlock(0, 1), makeBlock(0, 2), makeBlock(0, 3), makeBlock(0, 4),
		makeBlock(0, 5), makeBlock(0, 6), makeBlock(0, 7), makeBlock(0, 8),
	};
	mit.hash<8, 1>(buf);
	cout << "  hash<8,1>(buf)[0]   = " << buf[0] << "\n";
	cout << "  hash<8,1>(buf)[7]   = " << buf[7] << "\n";

	// (2) hash_cir<K, H> applies sigma() first, then hash. Used in half-gates:
	// a garbler hashes both labels of a half under one gid (H = 2), the
	// evaluator the one it holds (H = 1).
	mit.setS(S);  // reset for a fresh transcript
	block buf2[2] = {makeBlock(0, 0xAA), makeBlock(0, 0xBB)};
	mit.hash_cir<2, 1>(buf2);
	cout << "  hash_cir<2,1>[0]    = " << buf2[0] << "\n";

	// (3) A run of K lanes starts at a multiple of bit_ceil(K), so it lies in
	// one bucket: three hash_cir<3, 1> calls take gids 0..2, 4..6 and 8..10.
	mit.setS(S);
	block buf3[3] = {makeBlock(0, 1), makeBlock(0, 2), makeBlock(0, 3)};
	mit.hash_cir<3, 1>(buf3);
	cout << "  hash_cir<3,1>[0]    = " << buf3[0] << "\n";

	// (4) MITCCRHBulk::renew_ks(gid) repositions the gid stream, so a second
	// tweak dimension can be laid out as gid gaps: here gate 5 uses gids 40..43,
	// one lane per destination, each a distinct (S, gid) with its bucket and tweak.
	MITCCRHBulk<8> bulk;
	bulk.setS(S);
	block buf4[4] = {makeBlock(0, 1), makeBlock(0, 2), makeBlock(0, 3), makeBlock(0, 4)};
	bulk.renew_ks(uint64_t{5} * 8);
	bulk.hash_cir<4, 1>(buf4);
	cout << "  gate 5 / gid 40 lane 0 = " << buf4[0] << "\n";
}

// ---------- correctness ----------
//
// Reference, one block at a time:
//   ref(x, key, T) = y ^ AES_key(y),  y = (sigma(x) if cir else x) ^ T.
// key = S ^ makeBlock(gid >> RS, 0) and T = makeBlock(gid, 0); RS = 0 is the
// one-key-per-gid schedule, T still applied. The reference is independent in
// the construction (key derivation, tweak placement, Davies-Meyer), not in the
// cipher: AES_set_encrypt_key / AES_ecb_encrypt_blks are the library's own AES,
// pinned to the FIPS-197 vector in test_aes.

static block ref_hash(block x, block key, block T, bool cir) {
	block y = (cir ? sigma(x) : x) ^ T;
	AES_KEY ak;
	AES_set_encrypt_key(key, &ak);
	block c = y;
	AES_ecb_encrypt_blks<1>(&c, &ak);
	return c ^ y;
}
static block gid_key(block S, uint64_t g, int rs) { return S ^ makeBlock(g >> rs, 0); }
static block gid_tweak(uint64_t g) { return makeBlock(g, 0); }
static bool same(block a, block b) { return memcmp(&a, &b, sizeof(block)) == 0; }

static_assert(std::is_same_v<MITCCRH<>, MITCCRH<3, 8>>,
              "MITCCRH's defaults are ReuseShift = 3 (8x key reuse, 3-bit MI deduction) and Ahead = 8");
static_assert(std::is_same_v<MITCCRHBulk<8>, MITCCRHBulk<8, 3>>,
              "MITCCRHBulk's default ReuseShift is 3");

static void report(const string &what, bool ok) {
	cout << left << setw(64) << what << (ok ? "OK" : "FAIL") << "\n";
}

// ---- MITCCRH: the stream of single-use tweaks ----

// Model of the stream: a call of K lanes starts at the next multiple of
// bit_ceil(K) and leaves the stream just past its K gids.
struct StreamModel {
	uint64_t gid = 0;
	uint64_t take(int K) {
		const uint64_t a = std::bit_ceil((uint64_t)K);
		const uint64_t g0 = (gid + a - 1) & ~(a - 1);
		gid = g0 + (uint64_t)K;
		return g0;
	}
};

// `calls` calls of hash<K, H> (or hash_cir) against the per-block reference.
// A small Ahead makes the stream refill its key batch often, so calls cross
// batch boundaries, bucket boundaries and (for K not a power of two) the gids
// the alignment skips.
template <int K, int H, int RS, int A, bool Cir>
static bool check_stream_against_reference(int calls) {
	const block S = makeBlock(0xdeadbeefULL, 0xcafebabeULL);
	MITCCRH<RS, A> mit;
	mit.setS(S);
	StreamModel model;
	bool ok = true;
	for (int call = 0; call < calls; ++call) {
		block in[K * H], out[K * H];
		PRG().random_block(in, K * H);
		if constexpr (Cir) mit.template hash_cir<K, H>(out, in);
		else mit.template hash<K, H>(out, in);
		const uint64_t g0 = model.take(K);
		for (int k = 0; k < K; ++k)
			for (int h = 0; h < H; ++h) {
				const uint64_t g = g0 + (uint64_t)k;
				ok &= same(out[k * H + h], ref_hash(in[k * H + h], gid_key(S, g, RS), gid_tweak(g), Cir));
			}
	}
	ostringstream os;
	os << "  [MITCCRH<" << RS << "," << A << ">::" << (Cir ? "hash_cir<" : "hash<") << K << ","
	   << H << "> = reference]";
	report(os.str(), ok);
	return ok;
}

// Ahead decides only when keys are scheduled: three depths, one byte stream.
template <int K, int H>
static bool check_stream_ahead_invariance() {
	const block S = makeBlock(0x1111ULL, 0x2222ULL);
	MITCCRH<3, 1> a1;
	MITCCRH<3, 3> a3;
	MITCCRH<3, 8> a8;
	a1.setS(S); a3.setS(S); a8.setS(S);
	bool ok = true;
	for (int call = 0; call < 50; ++call) {
		block in[K * H], o1[K * H], o3[K * H], o8[K * H];
		PRG().random_block(in, K * H);
		a1.template hash_cir<K, H>(o1, in);
		a3.template hash_cir<K, H>(o3, in);
		a8.template hash_cir<K, H>(o8, in);
		ok &= memcmp(o1, o8, sizeof(o1)) == 0 && memcmp(o3, o8, sizeof(o3)) == 0;
	}
	ostringstream os;
	os << "  [MITCCRH Ahead 1/3/8, hash_cir<" << K << "," << H << ">: same bytes]";
	report(os.str(), ok);
	return ok;
}

// A stream and a bulk instance that take the same gids give the same bytes: for
// one power-of-two K per instance, MITCCRH<> hashes exactly as MITCCRHBulk<8>.
template <int K, int H>
static bool check_stream_matches_bulk() {
	static_assert(8 % K == 0, "one power-of-two K that divides the bulk batch");
	const block S = makeBlock(0x3333ULL, 0x4444ULL);
	MITCCRH<> stream;
	MITCCRHBulk<8> bulk;
	stream.setS(S); bulk.setS(S);
	bool ok = true;
	for (int call = 0; call < 50; ++call) {
		block in[K * H], os_[K * H], ob[K * H];
		PRG().random_block(in, K * H);
		stream.template hash<K, H>(os_, in);
		bulk.template hash<K, H>(ob, in);
		ok &= memcmp(os_, ob, sizeof(os_)) == 0;
	}
	ostringstream os;
	os << "  [MITCCRH = MITCCRHBulk<8>, hash<" << K << "," << H << ">]";
	report(os.str(), ok);
	return ok;
}

// setS in the middle of a key batch restarts the stream at gid 0 under the new
// S: the rest hashes as a fresh instance seeded with that S.
static bool check_stream_setS_restart() {
	const block S1 = makeBlock(0x5555ULL, 0x6666ULL), S2 = makeBlock(0x7777ULL, 0x8888ULL);
	MITCCRH<> mit, fresh;
	mit.setS(S1);
	block in[4], out[4], ref[4];
	for (int call = 0; call < 3; ++call) {   // stops inside the first key batch
		PRG().random_block(in, 4);
		mit.hash_cir<2, 2>(out, in);
	}
	mit.setS(S2);
	fresh.setS(S2);
	bool ok = true;
	for (int call = 0; call < 40; ++call) {
		PRG().random_block(in, 4);
		mit.hash_cir<2, 2>(out, in);
		fresh.hash_cir<2, 2>(ref, in);
		ok &= memcmp(out, ref, sizeof(out)) == 0;
	}
	ok &= same(mit.start_point(), S2);
	report("  [MITCCRH setS mid-batch restarts the stream]", ok);
	return ok;
}

// next_gid() is the first gid no hash has taken: it advances by K per call,
// after the call aligns its start to bit_ceil(K), and setS restarts it at 0.
static bool check_stream_next_gid() {
	MITCCRH<> mit;
	const block S1 = makeBlock(1, 2), S2 = makeBlock(3, 4);
	block b[8] = {};
	mit.setS(S1);
	bool ok = mit.next_gid() == 0 && same(mit.start_point(), S1);
	mit.hash<2, 1>(b);
	mit.hash<2, 1>(b);
	ok &= mit.next_gid() == 4;
	mit.hash<3, 1>(b);        // starts at 4 (a multiple of 4)
	ok &= mit.next_gid() == 7;
	mit.hash<8, 1>(b);        // starts at 8
	ok &= mit.next_gid() == 16;
	mit.hash<1, 1>(b);
	ok &= mit.next_gid() == 17;
	mit.setS(S2);
	ok &= mit.next_gid() == 0 && same(mit.start_point(), S2);
	report("  [MITCCRH next_gid / start_point]", ok);
	return ok;
}

// Mixed K on one stream: every call aligns to its own bit_ceil(K).
static bool check_stream_mixed_k() {
	const block S = makeBlock(0x9999ULL, 0xaaaaULL);
	MITCCRH<3, 2> mit;
	mit.setS(S);
	StreamModel model;
	bool ok = true;
	auto run = [&](auto k_tag) {
		constexpr int K = decltype(k_tag)::value;
		block in[K], out[K];
		PRG().random_block(in, K);
		mit.template hash<K, 1>(out, in);
		const uint64_t g0 = model.take(K);
		for (int k = 0; k < K; ++k)
			ok &= same(out[k], ref_hash(in[k], gid_key(S, g0 + (uint64_t)k, 3),
			                            gid_tweak(g0 + (uint64_t)k), false));
	};
	for (int rep = 0; rep < 6; ++rep) {
		run(std::integral_constant<int, 1>{});
		run(std::integral_constant<int, 3>{});
		run(std::integral_constant<int, 2>{});
		run(std::integral_constant<int, 8>{});
		run(std::integral_constant<int, 5>{});
	}
	report("  [MITCCRH mixed K: each call aligns to bit_ceil(K)]", ok);
	return ok;
}

// The point of T(gid): under the default shift, gids 0..7 share one AES key.
// Hashing the SAME block at each of them must give eight distinct values, each
// equal to the reference at its gid. (A bucketed hash without the input tweak
// returns one value eight times, which lets a half-gates evaluator XOR two
// gates' rows down to 0 or Delta — see test_halfgate.cpp.)
template <class Hash>
static bool check_same_block_distinct_within_bucket(const char *name) {
	const block S = makeBlock(0x2468ULL, 0x1357ULL);
	Hash mit;
	mit.setS(S);
	block x;
	PRG().random_block(&x, 1);

	block out[8];
	for (int g = 0; g < 8; ++g) {
		out[g] = x;
		mit.template hash_cir<1, 1>(out + g);
	}
	bool ok = true;
	for (int g = 0; g < 8; ++g) {
		ok &= same(out[g], ref_hash(x, gid_key(S, (uint64_t)g, 3), gid_tweak((uint64_t)g), true));
		for (int g2 = 0; g2 < g; ++g2) ok &= !same(out[g], out[g2]);
	}
	ostringstream os;
	os << "  [" << name << " same block, 8 gids of one bucket: distinct]";
	report(os.str(), ok);
	return ok;
}

// ---- properties both classes share ----

template <class Hash, int K, int H>
static bool check_hash_cir(const char *name) {
	const block S = makeBlock(0x1234ULL, 0x5678ULL);
	Hash a, b;
	a.setS(S); b.setS(S);

	block in[K * H];
	PRG().random_block(in, K * H);

	block via_cir[K * H], via_manual[K * H];
	memcpy(via_cir, in, sizeof(in));
	a.template hash_cir<K, H>(via_cir);

	for (int i = 0; i < K * H; ++i) via_manual[i] = sigma(in[i]);
	b.template hash<K, H>(via_manual);

	bool ok = memcmp(via_cir, via_manual, sizeof(in)) == 0;
	ostringstream os;
	os << "  [" << name << " hash_cir<" << K << "," << H << "> = hash(sigma(.))]";
	report(os.str(), ok);
	return ok;
}

template <class Hash, int K, int H, bool Cir>
static bool check_out_of_place(const char *name) {
	const block S = makeBlock(0x9999ULL, 0xaaaaULL);
	Hash in_place, out_place;
	in_place.setS(S); out_place.setS(S);

	bool ok = true;
	for (int call = 0; call < 3; ++call) {
		block in[K * H];
		PRG().random_block(in, K * H);
		block saved[K * H], via_in_place[K * H], via_out_place[K * H];
		memcpy(saved, in, sizeof(in));
		memcpy(via_in_place, in, sizeof(in));
		if constexpr (Cir) {
			in_place.template hash_cir<K, H>(via_in_place);
			out_place.template hash_cir<K, H>(via_out_place, in);
		} else {
			in_place.template hash<K, H>(via_in_place);
			out_place.template hash<K, H>(via_out_place, in);
		}
		ok &= memcmp(via_in_place, via_out_place, sizeof(in)) == 0 &&
		      memcmp(in, saved, sizeof(in)) == 0;
	}
	ostringstream os;
	os << "  [" << name << (Cir ? " hash_cir<" : " hash<") << K << "," << H
	   << "> out-of-place = in-place]";
	report(os.str(), ok);
	return ok;
}

// ---- MITCCRHBulk: the general form ----

// hash<K, H> from an unaligned gid0 = 7 (RS > 0 exercises the
// bucket-straddling renewal path), two calls back to back: the second call
// continues the schedule when K < B, and renews when K == B (into the same
// bucket when 2^RS > B, taking the cached schedule), against the per-block
// reference. The gid stream is contiguous across both cases, so lane 0 of
// the second call is gid0 + K.
template <int K, int H, int B = 8, int RS = 3>
static bool check_bulk_hash_against_reference() {
	static_assert(K <= B && B % K == 0, "MITCCRHBulk<B> requires K | B");
	const block S = makeBlock(0xdeadbeefULL, 0xcafebabeULL);
	const uint64_t gid0 = 7;

	MITCCRHBulk<B, RS> mit;
	mit.setS(S);
	mit.renew_ks(gid0);

	bool ok = true;
	for (int call = 0; call < 2; ++call) {
		block in[K * H], out[K * H];
		PRG().random_block(in, K * H);
		mit.template hash<K, H>(out, in);
		for (int k = 0; k < K; ++k)
			for (int h = 0; h < H; ++h) {
				const uint64_t g = gid0 + (uint64_t)(call * K + k);
				ok &= same(out[k * H + h],
				           ref_hash(in[k * H + h], gid_key(S, g, RS), gid_tweak(g), false));
			}
	}
	ostringstream os;
	os << "  [MITCCRHBulk<" << B << "," << RS << ">::hash<" << K << "," << H << "> = reference]";
	report(os.str(), ok);
	return ok;
}

// Default-shift bucketing across renewals: 8 consecutive hash<2,2> calls
// advance gid 0..15 = bucket 0 then bucket 1 (two renewals; the second batch
// goes through the single-bucket schedule-once path). Every block must equal
// the bucket-keyed, gid-tweaked reference.
static bool check_bulk_bucket_sequence() {
	const block S = makeBlock(0x7777ULL, 0x1111ULL);
	MITCCRHBulk<8> mit;        // default ReuseShift = 3
	mit.setS(S);

	bool ok = true;
	for (int call = 0; call < 8; ++call) {     // lanes for gids 2*call, 2*call+1
		block in[4];
		PRG().random_block(in, 4);
		block out[4];
		mit.hash<2, 2>(out, in);
		for (int k = 0; k < 2; ++k) {
			const uint64_t g = (uint64_t)(2 * call + k);
			for (int j = 0; j < 2; ++j)
				ok &= same(out[k * 2 + j],
				           ref_hash(in[k * 2 + j], gid_key(S, g, 3), gid_tweak(g), false));
		}
	}
	report("  [MITCCRHBulk default-shift buckets across renewals]", ok);
	return ok;
}

// renew_ks(gid) repositions the stream: afterwards lane k of the next hash is
// gid + k, bucket key and tweak included — the hook for laying a second tweak
// dimension out as gid gaps. Checked at a bucket-aligned gid (uniform batch)
// and at an unaligned one (the batch straddles two buckets).
static bool check_bulk_renew_ks_repositions() {
	const block S = makeBlock(0x5a5aULL, 0xa5a5ULL);
	bool ok = true;
	for (uint64_t g : {uint64_t{40}, uint64_t{1000003}}) {
		MITCCRHBulk<8> mit;
		mit.setS(S);
		block in[4], out[4];
		PRG().random_block(in, 4);
		mit.renew_ks(g);
		mit.hash_cir<4, 1>(out, in);
		for (int k = 0; k < 4; ++k)
			ok &= same(out[k], ref_hash(in[k], gid_key(S, g + k, 3), gid_tweak(g + k), true));
	}
	report("  [MITCCRHBulk renew_ks(gid) repositions the lanes]", ok);
	return ok;
}

// Out-of-place = in-place from unaligned gids (after renew_ks).
template <int K, int H, bool Cir>
static bool check_bulk_out_of_place_unaligned() {
	const block S = makeBlock(0xbbbbULL, 0xccccULL);
	const uint64_t gid0 = Cir ? 77 : 42;
	MITCCRHBulk<8> in_place, out_place;
	in_place.setS(S); out_place.setS(S);
	in_place.renew_ks(gid0); out_place.renew_ks(gid0);

	block in[K * H];
	PRG().random_block(in, K * H);
	block saved[K * H], via_in_place[K * H], via_out_place[K * H];
	memcpy(saved, in, sizeof(in));
	memcpy(via_in_place, in, sizeof(in));
	if constexpr (Cir) {
		in_place.template hash_cir<K, H>(via_in_place);
		out_place.template hash_cir<K, H>(via_out_place, in);
	} else {
		in_place.template hash<K, H>(via_in_place);
		out_place.template hash<K, H>(via_out_place, in);
	}
	bool ok = memcmp(via_in_place, via_out_place, sizeof(in)) == 0 &&
	          memcmp(in, saved, sizeof(in)) == 0;
	ostringstream os;
	os << "  [MITCCRHBulk" << (Cir ? " hash_cir<" : " hash<") << K << "," << H
	   << "> out-of-place = in-place, gid " << gid0 << "]";
	report(os.str(), ok);
	return ok;
}

// next_gid() through setS, partial and full schedules, renew_ks(gid), and a
// second setS; start_point() returns S.
static bool check_bulk_next_gid() {
	MITCCRHBulk<8> mit;
	const block S1 = makeBlock(1, 2), S2 = makeBlock(3, 4);
	block b[8] = {};
	mit.setS(S1);
	bool ok = mit.next_gid() == 0 && same(mit.start_point(), S1);
	mit.hash<2, 1>(b);
	mit.hash<2, 1>(b);
	ok &= mit.next_gid() == 4;
	mit.hash<4, 1>(b);
	ok &= mit.next_gid() == 8;    // schedule used up: the next one starts at 8
	mit.hash<2, 1>(b);
	ok &= mit.next_gid() == 10;
	mit.renew_ks(99);
	ok &= mit.next_gid() == 99;
	mit.hash<1, 1>(b);
	ok &= mit.next_gid() == 100;
	mit.setS(S2);
	ok &= mit.next_gid() == 0 && same(mit.start_point(), S2);
	report("  [MITCCRHBulk next_gid / start_point]", ok);
	return ok;
}

// One instance hashing with more than one K: hash<1,1>, then hash<2,1> twelve
// times. A hash whose K lanes do not fit the schedule's remaining lanes starts
// a new schedule and skips the leftover lane, so no lane reads past the
// schedule and no gid repeats. Every block is checked against the one-block
// reference at the gid this rule gives (run under ASan to see an over-read).
template <int RS>
static bool check_bulk_mixed_k() {
	const block S = makeBlock(0x5eedULL, 0xf00dULL);
	constexpr int B = 8;
	MITCCRHBulk<B, RS> mit;
	mit.setS(S);
	uint64_t sched0 = 0, next_sched = 0;   // model of the schedule
	int used = B;
	auto take = [&](int K) {
		if (used + K > B) { sched0 = next_sched; next_sched += B; used = 0; }
		const uint64_t g = sched0 + (uint64_t)used;
		used += K;
		return g;
	};
	bool ok = true;
	auto check = [&](const block* in, const block* out, int K, uint64_t g) {
		for (int k = 0; k < K; ++k)
			ok &= same(out[k], ref_hash(in[k], gid_key(S, g + (uint64_t)k, RS),
			                             gid_tweak(g + (uint64_t)k), false));
	};
	block in[2], out[2];
	PRG().random_block(in, 2);
	mit.template hash<1, 1>(out, in);
	check(in, out, 1, take(1));
	for (int call = 0; call < 12; ++call) {
		PRG().random_block(in, 2);
		mit.template hash<2, 1>(out, in);
		check(in, out, 2, take(2));
	}
	ok &= mit.next_gid() == sched0 + (uint64_t)used;
	ostringstream os;
	os << "  [MITCCRHBulk mixed K, ReuseShift " << RS << "]";
	report(os.str(), ok);
	return ok;
}

static bool run_correctness() {
	cout << "=== correctness: MITCCRH (stream) ===\n";
	bool ok = true;
	ok &= check_stream_against_reference<1, 1, 3, 8, false>(40);
	ok &= check_stream_against_reference<1, 4, 3, 3, true>(40);
	ok &= check_stream_against_reference<2, 1, 3, 1, true>(40);    // half-gate evaluator; one key per batch
	ok &= check_stream_against_reference<2, 2, 3, 8, true>(40);    // half-gate garbler
	ok &= check_stream_against_reference<4, 1, 3, 3, false>(40);
	ok &= check_stream_against_reference<8, 1, 3, 8, false>(40);   // COT receiver
	ok &= check_stream_against_reference<8, 2, 3, 2, false>(40);   // COT sender
	ok &= check_stream_against_reference<8, 4, 5, 8, true>(40);    // bucket wider than a call
	ok &= check_stream_against_reference<3, 1, 2, 3, true>(40);    // K not a power of two
	ok &= check_stream_against_reference<6, 2, 3, 8, true>(40);    // 6 lanes in an aligned run of 8
	ok &= check_stream_against_reference<6, 1, 3, 1, true>(40);
	ok &= check_stream_against_reference<14, 2, 4, 4, true>(30);
	ok &= check_stream_against_reference<1, 2, 0, 5, true>(40);    // ReuseShift 0: a key per gid
	ok &= check_stream_against_reference<1, 6, 3, 8, true>(40);    // many blocks per tweak
	ok &= check_stream_ahead_invariance<2, 2>();
	ok &= check_stream_ahead_invariance<6, 1>();
	ok &= check_stream_matches_bulk<1, 1>();
	ok &= check_stream_matches_bulk<2, 2>();
	ok &= check_stream_matches_bulk<4, 1>();
	ok &= check_stream_matches_bulk<8, 2>();
	ok &= check_stream_setS_restart();
	ok &= check_stream_next_gid();
	ok &= check_stream_mixed_k();
	ok &= check_same_block_distinct_within_bucket<MITCCRH<>>("MITCCRH");
	ok &= check_hash_cir<MITCCRH<>, 2, 1>("MITCCRH");
	ok &= check_hash_cir<MITCCRH<>, 2, 2>("MITCCRH");
	ok &= check_hash_cir<MITCCRH<>, 8, 1>("MITCCRH");
	ok &= check_out_of_place<MITCCRH<>, 1, 1, false>("MITCCRH");
	ok &= check_out_of_place<MITCCRH<>, 8, 4, false>("MITCCRH");
	ok &= check_out_of_place<MITCCRH<>, 2, 1, true>("MITCCRH");
	ok &= check_out_of_place<MITCCRH<>, 6, 2, true>("MITCCRH");

	cout << "=== correctness: MITCCRHBulk ===\n";
	ok &= check_bulk_next_gid();
	ok &= check_bulk_mixed_k<0>();
	ok &= check_bulk_mixed_k<3>();
	ok &= check_bulk_hash_against_reference<1, 1>();
	ok &= check_bulk_hash_against_reference<1, 4>();
	ok &= check_bulk_hash_against_reference<2, 1>();
	ok &= check_bulk_hash_against_reference<4, 1>();
	ok &= check_bulk_hash_against_reference<8, 1>();
	ok &= check_bulk_hash_against_reference<8, 4>();
	ok &= check_bulk_hash_against_reference<2, 2, 8, 0>();   // one-key-per-gid schedule
	ok &= check_bulk_hash_against_reference<8, 2, 8, 0>();
	ok &= check_bulk_hash_against_reference<2, 2, 8, 5>();   // bucket wider than the batch
	ok &= check_bulk_hash_against_reference<8, 2, 8, 5>();   // second renewal reuses the cached bucket schedule
	ok &= check_bulk_hash_against_reference<1, 3, 8, 3>();   // odd lane span
	ok &= check_bulk_hash_against_reference<4, 6, 4, 2>();   // B = 4; 24-block runs (grouped wide tiles on VAES-512)
	ok &= check_bulk_hash_against_reference<1, 4, 1, 3>();   // B = 1 (one lane per schedule)
	ok &= check_bulk_hash_against_reference<16, 1, 16, 3>(); // B = 16: every batch straddles two buckets
	ok &= check_bulk_bucket_sequence();
	ok &= check_bulk_renew_ks_repositions();
	ok &= check_same_block_distinct_within_bucket<MITCCRHBulk<8>>("MITCCRHBulk");
	ok &= check_hash_cir<MITCCRHBulk<8>, 2, 1>("MITCCRHBulk");
	ok &= check_hash_cir<MITCCRHBulk<8>, 2, 2>("MITCCRHBulk");
	ok &= check_hash_cir<MITCCRHBulk<8>, 8, 1>("MITCCRHBulk");
	ok &= check_bulk_out_of_place_unaligned<1, 1, false>();
	ok &= check_bulk_out_of_place_unaligned<8, 4, false>();
	ok &= check_bulk_out_of_place_unaligned<2, 1, true>();
	ok &= check_bulk_out_of_place_unaligned<8, 1, true>();
	return ok;
}

int main(int /*argc*/, char ** /*argv*/) {
	example();
	cout << "\n";
	if (!run_correctness()) {
		cerr << "CORRECTNESS FAILURE\n";
		return 1;
	}
	return 0;
}
