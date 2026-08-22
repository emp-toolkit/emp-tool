// crypto/mitccrh.h — Multi-Instance Tweakable CCRH (Guo–Katz–Wang–Yang 2019).
// A batched key schedule + AES-encrypt-then-XOR-input construction used by
// half-gates garbling. Read example() first; the rest is verification.
//
// What's in mitccrh.h:
//   MITCCRH<BatchSize>                       AES_KEY scheduled_key[B], gid, start_point
//   setS(s)                                  reset start_point + gid
//   renew_ks() / renew_ks(gid)               schedule B keys from gid sequence
//   renew_ks(const block * tweaks)           schedule B keys from explicit tweaks
//   hash<K, H>(blks)                         consume K scheduled keys, hash K*H blocks
//   hash_cir<K, H>(blks)                     sigma() each block first, then hash<K,H>

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

	// (1) setS + hash<K, H>: feed K*H blocks, the construction XORs input back
	// over the encrypted output (TMMO).
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

	// (3) renew_ks(tweaks): schedule from explicit tweaks instead of gid.
	mit.setS(S);
	block tweaks[8];
	for (int i = 0; i < 8; ++i) tweaks[i] = makeBlock(i + 100, 0);
	mit.renew_ks(tweaks);
	block buf3[8];
	for (int i = 0; i < 8; ++i) buf3[i] = makeBlock(i, i);
	mit.hash<8, 1>(buf3);
	cout << "  hash<8,1> w/ tweaks[0] = " << buf3[0] << "\n";
}

// ---------- correctness ----------
//
// Reference: keys[i] = S ^ makeBlock((gid0+i) >> RS, 0) — the bucketed tweak
// (RS = ReuseShift; RS = 0 is the strict one-key-per-gid construction);
// schedule all B with AES_opt_key_schedule<B>; ParaEnc<K, H> consumes
// scheduled_key[0..K), then MITCCRH XORs the ciphertext with the original
// input. gid0 = 7 is deliberately unaligned so RS > 0 exercises the
// bucket-straddling renewal path.

static_assert(std::is_same_v<MITCCRH<8>, MITCCRH<8, 3>>,
              "MITCCRH's default ReuseShift is 3 (8x key reuse, 3-bit MI deduction)");

template <int K, int H, int B = 8, int RS = 3>
static bool check_mitccrh_against_paraenc() {
	static_assert(K <= B && B % K == 0, "MITCCRH<B> requires K | B");
	const block S = makeBlock(0xdeadbeefULL, 0xcafebabeULL);
	const uint64_t gid0 = 7;

	MITCCRH<B, RS> mit;
	mit.setS(S);
	mit.gid = gid0;
	mit.key_used = B;  // force a fresh schedule on first hash()

	block in[K * H];
	PRG().random_block(in, K * H);

	block emp_out[K * H];
	memcpy(emp_out, in, sizeof(in));
	mit.template hash<K, H>(emp_out);

	block ref_keys[B];
	for (int i = 0; i < B; ++i)
		ref_keys[i] = S ^ makeBlock((gid0 + (uint64_t)i) >> RS, 0);
	AES_KEY skeys[B];
	AES_opt_key_schedule<B>(ref_keys, skeys);

	block ref_out[K * H];
	memcpy(ref_out, in, sizeof(in));
	ParaEnc<K, H>(ref_out, skeys);
	for (int i = 0; i < K * H; ++i) ref_out[i] = ref_out[i] ^ in[i];

	bool ok = memcmp(emp_out, ref_out, sizeof(in)) == 0;
	ostringstream os;
	os << "  [MITCCRH<" << B << "," << RS << ">::hash<" << K << "," << H << "> = ParaEnc^in]";
	cout << left << setw(46) << os.str() << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

// Default-shift bucketing across renewals: 8 consecutive hash<2,2> calls
// advance gid 0..15 = bucket 0 then bucket 1 (two renewals; the second batch
// goes through the single-bucket schedule-once/replicate path). Every block
// must equal the bucket-keyed single-AES reference:
// out = AES_{S ^ (gid>>3)}(in) ^ in.
static bool check_bucket_sequence() {
	const block S = makeBlock(0x7777ULL, 0x1111ULL);
	MITCCRH<8> mit;        // default ReuseShift = 3
	mit.setS(S);

	bool ok = true;
	for (int call = 0; call < 8; ++call) {     // keys for gids 2*call, 2*call+1
		block in[4];
		PRG().random_block(in, 4);
		block out[4];
		mit.hash<2, 2>(out, in);
		for (int k = 0; k < 2; ++k) {
			uint64_t g = (uint64_t)(2 * call + k);
			block key = S ^ makeBlock(g >> 3, 0);
			AES_KEY ak;
			AES_set_encrypt_key(key, &ak);
			for (int j = 0; j < 2; ++j) {
				block ref = in[k * 2 + j];
				AES_ecb_encrypt_blks<1>(&ref, &ak);
				ref = ref ^ in[k * 2 + j];
				ok &= memcmp(&ref, &out[k * 2 + j], sizeof(block)) == 0;
			}
		}
	}
	cout << "  [default-shift buckets across renewals]      " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

template <int K, int H>
static bool check_hash_cir() {
	const block S = makeBlock(0x1234ULL, 0x5678ULL);
	MITCCRH<8> a, b;
	a.setS(S); b.setS(S);
	a.gid = b.gid = 0;
	a.key_used = b.key_used = 8;

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

// hash_cir_fixed<K,H> contract: hashes under the keys
// from an explicit renew_ks(tweaks) WITHOUT consuming them. Checks:
// (1) equivalence with the first consuming hash_cir under the same
// schedule; (2) repeated fixed calls reproduce the output and leave
// key_used untouched, so a later hash_cir still starts at key 0;
// (3) a different tweak vector changes every hashed lane.
template <int K, int H>
static bool check_hash_cir_fixed() {
	const block S = makeBlock(0xf1e2ULL, 0xd3c4ULL);
	PRG prg(&zero_block);
	block tweaks[8], tweaks2[8], in[K * H];
	prg.random_block(tweaks, 8);
	prg.random_block(tweaks2, 8);
	prg.random_block(in, K * H);

	MITCCRH<8> a, b, c;
	a.setS(S); b.setS(S); c.setS(S);
	a.renew_ks(tweaks);
	b.renew_ks(tweaks);
	c.renew_ks(tweaks2);

	block fixed_out[K * H], cir_out[K * H], again[K * H], after[K * H],
	    other[K * H];
	memcpy(fixed_out, in, sizeof(in));
	memcpy(cir_out, in, sizeof(in));
	memcpy(again, in, sizeof(in));
	memcpy(after, in, sizeof(in));
	memcpy(other, in, sizeof(in));

	a.template hash_cir_fixed<K, H>(fixed_out);
	b.template hash_cir<K, H>(cir_out);
	bool ok = memcmp(fixed_out, cir_out, sizeof(in)) == 0;

	a.template hash_cir_fixed<K, H>(again);
	ok &= memcmp(again, fixed_out, sizeof(in)) == 0;

	a.template hash_cir<K, H>(after);
	ok &= memcmp(after, cir_out, sizeof(in)) == 0;

	c.template hash_cir_fixed<K, H>(other);
	for (int i = 0; i < K * H; ++i)
		ok &= memcmp(&other[i], &fixed_out[i], sizeof(block)) != 0;

	ostringstream os;
	os << "  [hash_cir_fixed<" << K << "," << H << "> contract]";
	cout << left << setw(46) << os.str() << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

template <int K, int H>
static bool check_out_of_place() {
	const block S = makeBlock(0x9999ULL, 0xaaaaULL);
	const uint64_t gid0 = 42;
	MITCCRH<8> in_place, out_place;
	in_place.setS(S); out_place.setS(S);
	in_place.gid = out_place.gid = gid0;
	in_place.key_used = out_place.key_used = 8;

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
	in_place.gid = out_place.gid = gid0;
	in_place.key_used = out_place.key_used = 8;

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

static bool check_setS_resets_gid() {
	MITCCRH<8> mit;
	mit.setS(makeBlock(1, 2));
	mit.gid = 99;
	mit.setS(makeBlock(3, 4));
	bool ok = (mit.gid == 0);
	cout << "  [setS resets gid]                              " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_default_seed_is_zero() {
	MITCCRH<8> implicit_zero, explicit_zero;
	explicit_zero.setS(zero_block);
	block implicit_out[8], explicit_out[8];
	for (int i = 0; i < 8; ++i)
		implicit_out[i] = explicit_out[i] = makeBlock(i, i + 1);
	implicit_zero.hash<8, 1>(implicit_out);
	explicit_zero.hash<8, 1>(explicit_out);
	bool ok = memcmp(implicit_out, explicit_out, sizeof(implicit_out)) == 0;
	cout << "  [default S is zero]                            " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	bool ok = true;
	ok &= check_setS_resets_gid();
	ok &= check_default_seed_is_zero();
	ok &= check_mitccrh_against_paraenc<1, 1>();
	ok &= check_mitccrh_against_paraenc<1, 4>();
	ok &= check_mitccrh_against_paraenc<2, 1>();
	ok &= check_mitccrh_against_paraenc<4, 1>();
	ok &= check_mitccrh_against_paraenc<8, 1>();
	ok &= check_mitccrh_against_paraenc<8, 4>();
	ok &= check_mitccrh_against_paraenc<2, 2, 8, 0>();   // strict per-gid construction
	ok &= check_mitccrh_against_paraenc<8, 2, 8, 0>();
	ok &= check_mitccrh_against_paraenc<2, 2, 8, 5>();   // bucket spans renewals (cache path)
	ok &= check_bucket_sequence();
	ok &= check_hash_cir<2, 1>();
	ok &= check_hash_cir<2, 2>();
	ok &= check_hash_cir<8, 1>();
	ok &= check_hash_cir_fixed<2, 1>();
	ok &= check_hash_cir_fixed<8, 1>();
	ok &= check_hash_cir_fixed<8, 2>();
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
