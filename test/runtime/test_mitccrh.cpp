// crypto/mitccrh.h — Multi-Instance Tweakable CCRH (Guo–Katz–Wang–Weng–Yu,
// eprint 2019/1168). A bucketed-key Davies-Meyer AES hash used by half-gates
// garbling. Read example() first; the rest is verification.
//
// What's in mitccrh.h:
//   MITCCRH<BatchSize, ReuseShift>           start_point(), next_gid()
//   setS(s)                                  install S, restart the gid stream at 0
//   renew_ks(gid)                            schedule B keys from gid
//   hash<K, H>(blks)                         consume K lanes, hash K*H blocks:
//                                              y ^ AES_{S ^ (gid>>RS)}(y), y = x ^ T(gid)
//   hash_cir<K, H>(blks)                     same with y = sigma(x) ^ T(gid)
// T(gid) = makeBlock(gid, 0): the gid enters the cipher input, so gids that
// share one bucket key (RS > 0) still hash the same block to distinct values.

#include "emp-tool/emp-tool.h"

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
	MITCCRH<8> mit;
	mit.setS(S);

	block buf[8] = {
		makeBlock(0, 1), makeBlock(0, 2), makeBlock(0, 3), makeBlock(0, 4),
		makeBlock(0, 5), makeBlock(0, 6), makeBlock(0, 7), makeBlock(0, 8),
	};
	mit.hash<8, 1>(buf);
	cout << "  hash<8,1>(buf)[0]   = " << buf[0] << "\n";
	cout << "  hash<8,1>(buf)[7]   = " << buf[7] << "\n";

	// (2) hash_cir<K, H> applies sigma() first, then hash. Used in half-gates.
	mit.setS(S);  // reset for a fresh transcript
	block buf2[2] = {makeBlock(0, 0xAA), makeBlock(0, 0xBB)};
	mit.hash_cir<2, 1>(buf2);
	cout << "  hash_cir<2,1>[0]    = " << buf2[0] << "\n";

	// (3) renew_ks(gid) repositions the gid stream, so a second tweak dimension
	// can be laid out as gid gaps: here gate 5 uses gids 40..43, one lane per
	// destination, each a distinct (S, gid) with its bucket and tweak.
	mit.setS(S);
	block buf3[4] = {makeBlock(0, 1), makeBlock(0, 2), makeBlock(0, 3), makeBlock(0, 4)};
	mit.renew_ks(uint64_t{5} * 8);
	mit.hash_cir<4, 1>(buf3);
	cout << "  gate 5 / gid 40 lane 0 = " << buf3[0] << "\n";
}

// ---------- correctness ----------
//
// Reference, one block at a time:
//   ref(x, key, T) = y ^ AES_key(y),  y = (sigma(x) if cir else x) ^ T.
// key = S ^ makeBlock(gid >> RS, 0) and T = makeBlock(gid, 0); RS = 0 is the
// one-key-per-gid schedule, T still applied. Each lane k of a hash<K, H> call
// covers gid (first gid of the schedule) + key_used + k. The reference is
// independent in the construction (key derivation, tweak placement,
// Davies-Meyer), not in the cipher: AES_set_encrypt_key / AES_ecb_encrypt_blks
// are the library's own AES, pinned to the FIPS-197 vector in test_aes.

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

static_assert(std::is_same_v<MITCCRH<8>, MITCCRH<8, 3>>,
              "MITCCRH's default ReuseShift is 3 (8x key reuse, 3-bit MI deduction)");

// hash<K, H> from an unaligned gid0 = 7 (RS > 0 exercises the
// bucket-straddling renewal path), two calls back to back: the second call
// continues the schedule when K < B, and renews when K == B (into the same
// bucket when 2^RS > B, taking the cached schedule), against the per-block
// reference. The gid stream is contiguous across both cases, so lane 0 of
// the second call is gid0 + K.
template <int K, int H, int B = 8, int RS = 3>
static bool check_hash_against_reference() {
	static_assert(K <= B && B % K == 0, "MITCCRH<B> requires K | B");
	const block S = makeBlock(0xdeadbeefULL, 0xcafebabeULL);
	const uint64_t gid0 = 7;

	MITCCRH<B, RS> mit;
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
	os << "  [MITCCRH<" << B << "," << RS << ">::hash<" << K << "," << H << "> = reference]";
	cout << left << setw(46) << os.str() << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

// Default-shift bucketing across renewals: 8 consecutive hash<2,2> calls
// advance gid 0..15 = bucket 0 then bucket 1 (two renewals; the second batch
// goes through the single-bucket schedule-once path). Every block must equal
// the bucket-keyed, gid-tweaked reference.
static bool check_bucket_sequence() {
	const block S = makeBlock(0x7777ULL, 0x1111ULL);
	MITCCRH<8> mit;        // default ReuseShift = 3
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
	cout << "  [default-shift buckets across renewals]      " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

// The point of T(gid): under the default shift, gids 0..7 share one AES key.
// Hashing the SAME block at each of them must give eight distinct values, each
// equal to the reference at its gid. (A bucketed hash without the input tweak
// returns one value eight times, which lets a half-gates evaluator XOR two
// gates' rows down to 0 or Delta — see test_halfgate.cpp.)
static bool check_same_block_distinct_within_bucket() {
	const block S = makeBlock(0x2468ULL, 0x1357ULL);
	MITCCRH<8> mit;
	mit.setS(S);
	block x;
	PRG().random_block(&x, 1);

	block out[8];
	for (int g = 0; g < 8; ++g) {
		out[g] = x;
		mit.hash_cir<1, 1>(out + g);
	}
	bool ok = true;
	for (int g = 0; g < 8; ++g) {
		ok &= same(out[g], ref_hash(x, gid_key(S, (uint64_t)g, 3), gid_tweak((uint64_t)g), true));
		for (int g2 = 0; g2 < g; ++g2) ok &= !same(out[g], out[g2]);
	}
	cout << "  [same block, 8 gids of one bucket: distinct]  " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

template <int K, int H>
static bool check_hash_cir() {
	const block S = makeBlock(0x1234ULL, 0x5678ULL);
	MITCCRH<8> a, b;
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
	os << "  [hash_cir<" << K << "," << H << "> = hash(sigma(.))]";
	cout << left << setw(46) << os.str() << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

// renew_ks(gid) repositions the stream: afterwards lane k of the next hash is
// gid + k, bucket key and tweak included — the hook for laying a second tweak
// dimension out as gid gaps. Checked at a bucket-aligned gid (uniform batch)
// and at an unaligned one (the batch straddles two buckets).
static bool check_renew_ks_repositions() {
	const block S = makeBlock(0x5a5aULL, 0xa5a5ULL);
	bool ok = true;
	for (uint64_t g : {uint64_t{40}, uint64_t{1000003}}) {
		MITCCRH<8> mit;
		mit.setS(S);
		block in[4], out[4];
		PRG().random_block(in, 4);
		mit.renew_ks(g);
		mit.hash_cir<4, 1>(out, in);
		for (int k = 0; k < 4; ++k)
			ok &= same(out[k], ref_hash(in[k], gid_key(S, g + k, 3), gid_tweak(g + k), true));
	}
	cout << "  [renew_ks(gid) repositions the lanes]        " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

template <int K, int H>
static bool check_out_of_place() {
	const block S = makeBlock(0x9999ULL, 0xaaaaULL);
	const uint64_t gid0 = 42;
	MITCCRH<8> in_place, out_place;
	in_place.setS(S); out_place.setS(S);
	in_place.renew_ks(gid0); out_place.renew_ks(gid0);

	block in[K * H];
	PRG().random_block(in, K * H);
	block saved[K * H], via_in_place[K * H], via_out_place[K * H];
	memcpy(saved, in, sizeof(in));
	memcpy(via_in_place, in, sizeof(in));

	in_place.template hash<K, H>(via_in_place);
	out_place.template hash<K, H>(via_out_place, in);

	bool ok = memcmp(via_in_place, via_out_place, sizeof(in)) == 0 &&
	          memcmp(in, saved, sizeof(in)) == 0;
	ostringstream os;
	os << "  [hash<" << K << "," << H << "> out-of-place = in-place]";
	cout << left << setw(46) << os.str() << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

template <int K, int H>
static bool check_hash_cir_out_of_place() {
	const block S = makeBlock(0xbbbbULL, 0xccccULL);
	const uint64_t gid0 = 77;
	MITCCRH<8> in_place, out_place;
	in_place.setS(S); out_place.setS(S);
	in_place.renew_ks(gid0); out_place.renew_ks(gid0);

	block in[K * H];
	PRG().random_block(in, K * H);
	block saved[K * H], via_in_place[K * H], via_out_place[K * H];
	memcpy(saved, in, sizeof(in));
	memcpy(via_in_place, in, sizeof(in));

	in_place.template hash_cir<K, H>(via_in_place);
	out_place.template hash_cir<K, H>(via_out_place, in);

	bool ok = memcmp(via_in_place, via_out_place, sizeof(in)) == 0 &&
	          memcmp(in, saved, sizeof(in)) == 0;
	ostringstream os;
	os << "  [hash_cir<" << K << "," << H << "> out-of-place = in-place]";
	cout << left << setw(46) << os.str() << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

// next_gid() through setS, partial and full schedules, renew_ks(gid), and a
// second setS; start_point() returns S.
static bool check_next_gid() {
	MITCCRH<8> mit;
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
	cout << "  [next_gid / start_point]                       " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

// One instance hashing with more than one K: hash<1,1>, then hash<2,1> twelve
// times. A hash whose K lanes do not fit the schedule's remaining lanes starts
// a new schedule and skips the leftover lane, so no lane reads past the
// schedule and no gid repeats. Every block is checked against the one-block
// reference at the gid this rule gives (run under ASan to see an over-read).
template <int RS>
static bool check_mixed_k() {
	const block S = makeBlock(0x5eedULL, 0xf00dULL);
	constexpr int B = 8;
	MITCCRH<B, RS> mit;
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
	cout << "  [mixed K, ReuseShift " << RS << "]                          "
	     << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	bool ok = true;
	ok &= check_next_gid();
	ok &= check_mixed_k<0>();
	ok &= check_mixed_k<3>();
	ok &= check_hash_against_reference<1, 1>();
	ok &= check_hash_against_reference<1, 4>();
	ok &= check_hash_against_reference<2, 1>();
	ok &= check_hash_against_reference<4, 1>();
	ok &= check_hash_against_reference<8, 1>();
	ok &= check_hash_against_reference<8, 4>();
	ok &= check_hash_against_reference<2, 2, 8, 0>();   // one-key-per-gid schedule
	ok &= check_hash_against_reference<8, 2, 8, 0>();
	ok &= check_hash_against_reference<2, 2, 8, 5>();   // bucket wider than the batch
	ok &= check_hash_against_reference<8, 2, 8, 5>();   // second renewal reuses the cached bucket schedule
	ok &= check_hash_against_reference<1, 3, 8, 3>();   // odd lane span
	ok &= check_hash_against_reference<4, 6, 4, 2>();   // B = 4; 24-block runs (grouped wide tiles on VAES-512)
	ok &= check_hash_against_reference<1, 4, 1, 3>();   // B = 1 (one lane per schedule)
	ok &= check_hash_against_reference<16, 1, 16, 3>(); // B = 16: every batch straddles two buckets
	ok &= check_bucket_sequence();
	ok &= check_same_block_distinct_within_bucket();
	ok &= check_hash_cir<2, 1>();
	ok &= check_hash_cir<2, 2>();
	ok &= check_hash_cir<8, 1>();
	ok &= check_renew_ks_repositions();
	ok &= check_out_of_place<1, 1>();
	ok &= check_out_of_place<8, 4>();
	ok &= check_hash_cir_out_of_place<2, 1>();
	ok &= check_hash_cir_out_of_place<8, 1>();
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
