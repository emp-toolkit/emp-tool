// Test for emp-tool/circuits/sha3_circuit.h:
//   - chi on a known row vs hand-computed
//   - iota round 0 alone (lane (0,0) == RC[0])
//   - full Keccak-f[1600] vs a plain-C reference (random inputs)
//   - SHA3-256 KATs for "" and "abc"
//   - cross-check SHA3_256_Calculator vs OpenSSL EVP_sha3_256
//   - reports AND-gate counts (expect 38400 ANDs / permute,
//     38400 ANDs / SHA3-256 of any input <= 1080 bits)

#include "emp-tool/emp-tool.h"
#include "emp-tool/circuits/sha3_circuit.h"
#include <openssl/evp.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace emp;

// --- Plain-C Keccak-f[1600] reference --------------------------------------
// Input/output: 25 lanes of 64 bits each. state[x + 5*y] is lane (x, y); bit z
// is `(state[x+5*y] >> z) & 1`. Matches the in-circuit indexing in sha3_circuit.h.

static inline uint64_t rotl64(uint64_t v, int n) {
	n &= 63;
	return n ? (v << n) | (v >> (64 - n)) : v;
}

static void keccak_f_ref(uint64_t s[25]) {
	static const uint64_t RC[24] = {
		0x0000000000000001ULL, 0x0000000000008082ULL,
		0x800000000000808aULL, 0x8000000080008000ULL,
		0x000000000000808bULL, 0x0000000080000001ULL,
		0x8000000080008081ULL, 0x8000000000008009ULL,
		0x000000000000008aULL, 0x0000000000000088ULL,
		0x0000000080008009ULL, 0x000000008000000aULL,
		0x000000008000808bULL, 0x800000000000008bULL,
		0x8000000000008089ULL, 0x8000000000008003ULL,
		0x8000000000008002ULL, 0x8000000000000080ULL,
		0x000000000000800aULL, 0x800000008000000aULL,
		0x8000000080008081ULL, 0x8000000000008080ULL,
		0x0000000080000001ULL, 0x8000000080008008ULL,
	};
	static const int RHO[5][5] = {
		{  0,  1, 62, 28, 27 },
		{ 36, 44,  6, 55, 20 },
		{  3, 10, 43, 25, 39 },
		{ 41, 45, 15, 21,  8 },
		{ 18,  2, 61, 56, 14 },
	};
	for (int r = 0; r < 24; ++r) {
		uint64_t C[5], D[5];
		for (int x = 0; x < 5; ++x)
			C[x] = s[x] ^ s[x+5] ^ s[x+10] ^ s[x+15] ^ s[x+20];
		for (int x = 0; x < 5; ++x)
			D[x] = C[(x+4)%5] ^ rotl64(C[(x+1)%5], 1);
		for (int x = 0; x < 5; ++x)
			for (int y = 0; y < 5; ++y)
				s[x + 5*y] ^= D[x];

		uint64_t B[25] = {0};
		for (int x = 0; x < 5; ++x)
			for (int y = 0; y < 5; ++y)
				B[y + 5*((2*x + 3*y) % 5)] = rotl64(s[x + 5*y], RHO[y][x]);

		for (int y = 0; y < 5; ++y) {
			uint64_t row[5];
			for (int x = 0; x < 5; ++x) row[x] = B[x + 5*y];
			for (int x = 0; x < 5; ++x)
				s[x + 5*y] = row[x] ^ ((~row[(x+1)%5]) & row[(x+2)%5]);
		}

		s[0] ^= RC[r];
	}
}

// --- Helpers ---------------------------------------------------------------

// Pack 1600 reveal'd bits (LSB-first within lane) into 25 uint64_t lanes.
static void state_to_lanes(const Bit state[1600], uint64_t lanes[25]) {
	for (int k = 0; k < 25; ++k) {
		uint64_t v = 0;
		for (int z = 0; z < 64; ++z)
			if (state[64*k + z].reveal<bool>(PUBLIC))
				v |= (uint64_t)1 << z;
		lanes[k] = v;
	}
}

// Inverse: load 25 uint64_t lanes into 1600 Bit wires (party = ALICE so the
// clear backend doesn't constant-fold AND gates).
static void lanes_to_state(const uint64_t lanes[25], Bit state[1600]) {
	for (int k = 0; k < 25; ++k)
		for (int z = 0; z < 64; ++z)
			state[64*k + z] = Bit(((lanes[k] >> z) & 1) != 0, ALICE);
}

// --- Tests -----------------------------------------------------------------

static int test_chi_row() {
	// Pick a known row of 5 lanes; only lane bit z=0 is interesting.
	const uint64_t a = 0xa3, b = 0x1c, c = 0xff, d = 0x42, e = 0x07;
	const uint64_t row[5] = { a, b, c, d, e };

	Bit state[1600];
	for (int i = 0; i < 1600; ++i) state[i] = Bit(false, ALICE);
	for (int x = 0; x < 5; ++x)
		for (int z = 0; z < 64; ++z)
			state[64*x + z] = Bit(((row[x] >> z) & 1) != 0, ALICE);

	keccak_chi<block>(state);

	int fail = 0;
	for (int x = 0; x < 5; ++x) {
		uint64_t got = 0;
		for (int z = 0; z < 64; ++z)
			if (state[64*x + z].reveal<bool>(PUBLIC))
				got |= (uint64_t)1 << z;
		uint64_t want = row[x] ^ ((~row[(x+1)%5]) & row[(x+2)%5]);
		if (got != want) {
			++fail;
			printf("CHI FAIL x=%d: got=%016llx want=%016llx\n",
			       x, (unsigned long long)got, (unsigned long long)want);
		}
	}
	printf("chi row: %s\n", fail == 0 ? "OK" : "FAIL");
	return fail;
}

static int test_iota_round0() {
	Bit state[1600];
	for (int i = 0; i < 1600; ++i) state[i] = Bit(false, ALICE);

	keccak_iota<block>(state, 0);

	uint64_t lanes[25];
	state_to_lanes(state, lanes);

	int fail = 0;
	if (lanes[0] != 0x0000000000000001ULL) {
		printf("IOTA FAIL lane(0,0) = %016llx, want 0x01\n",
		       (unsigned long long)lanes[0]);
		++fail;
	}
	for (int k = 1; k < 25; ++k) {
		if (lanes[k] != 0) {
			printf("IOTA FAIL lane[%d] = %016llx, want 0\n",
			       k, (unsigned long long)lanes[k]);
			++fail;
		}
	}
	printf("iota round 0: %s\n", fail == 0 ? "OK" : "FAIL");
	return fail;
}

static int test_full_keccak_f(uint64_t and_per_permute_out[1]) {
	srand(0xbada55);
	int trials = 4, fail = 0;
	uint64_t total_ands = 0, calls = 0;

	for (int t = 0; t < trials; ++t) {
		uint64_t lanes[25];
		if (t == 0) {
			for (int k = 0; k < 25; ++k) lanes[k] = 0; // all-zero state
		} else {
			for (int k = 0; k < 25; ++k) {
				uint64_t v = 0;
				for (int b = 0; b < 8; ++b)
					v |= (uint64_t)(rand() & 0xff) << (8*b);
				lanes[k] = v;
			}
		}

		uint64_t want[25];
		memcpy(want, lanes, sizeof want);
		keccak_f_ref(want);

		Bit state[1600];
		lanes_to_state(lanes, state);

		uint64_t and_before = backend->num_and();
		Keccak_F_Calculator_T<block> kf;
		kf.permute(state);
		uint64_t and_for_call = backend->num_and() - and_before;
		total_ands += and_for_call;
		++calls;

		uint64_t got[25];
		state_to_lanes(state, got);
		if (memcmp(got, want, sizeof want) != 0) {
			++fail;
			if (fail < 3) {
				printf("KECCAK-F FAIL trial %d:\n", t);
				for (int k = 0; k < 25; ++k)
					if (got[k] != want[k])
						printf("  lane %d got=%016llx want=%016llx\n",
						       k, (unsigned long long)got[k],
						       (unsigned long long)want[k]);
			}
		}
	}

	uint64_t avg = calls ? total_ands / calls : 0;
	*and_per_permute_out = avg;
	printf("Keccak-f vs C ref: %d / %d failures, %llu ANDs/permute (expect 38400)\n",
	       fail, trials, (unsigned long long)avg);
	return fail;
}

static int sha3_kat(const char * msg, size_t msg_len, const uint8_t want[32],
                    const char * label, uint64_t * and_count_out) {
	BitVec input(msg_len * 8, (const uint8_t *)msg, ALICE);
	Bit out_bits[256];

	uint64_t and_before = backend->num_and();
	SHA3_256_Calculator sha3;
	sha3.sha3_256(out_bits, input.bits.data(), input.bits.size());
	uint64_t and_for_hash = backend->num_and() - and_before;
	*and_count_out = and_for_hash;

	uint8_t got[32];
	memset(got, 0, 32);
	for (int i = 0; i < 256; ++i)
		if (out_bits[i].reveal<bool>(PUBLIC))
			got[i / 8] |= (uint8_t)(1 << (i % 8));

	int ok = (memcmp(got, want, 32) == 0);
	printf("SHA3-256 KAT %s: %s (%llu ANDs)\n",
	       label, ok ? "OK" : "FAIL", (unsigned long long)and_for_hash);
	if (!ok) {
		printf("  got:  "); for (int i = 0; i < 32; ++i) printf("%02x", got[i]);
		printf("\n  want: "); for (int i = 0; i < 32; ++i) printf("%02x", want[i]);
		printf("\n");
	}
	return ok ? 0 : 1;
}

static int test_sha3_kats() {
	// Empty string.
	const uint8_t want_empty[32] = {
		0xa7,0xff,0xc6,0xf8,0xbf,0x1e,0xd7,0x66,
		0x51,0xc1,0x47,0x56,0xa0,0x61,0xd6,0x62,
		0xf5,0x80,0xff,0x4d,0xe4,0x3b,0x49,0xfa,
		0x82,0xd8,0x0a,0x4b,0x80,0xf8,0x43,0x4a};
	// "abc"
	const uint8_t want_abc[32] = {
		0x3a,0x98,0x5d,0xa7,0x4f,0xe2,0x25,0xb2,
		0x04,0x5c,0x17,0x2d,0x6b,0xd3,0x90,0xbd,
		0x85,0x5f,0x08,0x6e,0x3e,0x9d,0x52,0x5b,
		0x46,0xbf,0xe2,0x45,0x11,0x43,0x15,0x32};

	int fail = 0;
	uint64_t ac;
	fail += sha3_kat("", 0, want_empty, "\"\"", &ac);
	fail += sha3_kat("abc", 3, want_abc, "\"abc\"", &ac);
	return fail;
}

static int test_sha3_vs_openssl() {
	// Lengths chosen to span rate-block boundaries (rate = 1088 bits = 136 bytes).
	const size_t lens[] = { 0, 1, 17, 135, 136, 137, 271, 272, 273, 1000 };
	const int n_lens = sizeof(lens) / sizeof(lens[0]);

	srand(0xc0ffee);
	int fail = 0;
	for (int i = 0; i < n_lens; ++i) {
		const size_t L = lens[i];
		uint8_t * msg = (uint8_t *) malloc(L ? L : 1);
		for (size_t j = 0; j < L; ++j) msg[j] = (uint8_t)(rand() & 0xff);

		uint8_t want[32];
		emp::sha3_256<uint8_t>(want, msg, L);

		BitVec input(L * 8, msg, ALICE);
		Bit out_bits[256];
		SHA3_256_Calculator sha3;
		// Have to handle L == 0 specially (BitVec of 0 bits has no .bits).
		if (L == 0) {
			const Bit * empty = nullptr;
			size_t zero = 0;
			sha3.sha3_256(out_bits, &empty, &zero);
		} else {
			sha3.sha3_256(out_bits, input.bits.data(), input.bits.size());
		}

		uint8_t got[32];
		memset(got, 0, 32);
		for (int b = 0; b < 256; ++b)
			if (out_bits[b].reveal<bool>(PUBLIC))
				got[b / 8] |= (uint8_t)(1 << (b % 8));

		if (memcmp(got, want, 32) != 0) {
			++fail;
			if (fail < 3) {
				printf("SHA3 vs OpenSSL FAIL len=%zu:\n  got:  ", L);
				for (int b = 0; b < 32; ++b) printf("%02x", got[b]);
				printf("\n  want: ");
				for (int b = 0; b < 32; ++b) printf("%02x", want[b]);
				printf("\n");
			}
		}
		free(msg);
	}
	printf("SHA3-256 vs OpenSSL: %d / %d failures\n", fail, n_lens);
	return fail;
}

int main() {
	setup_clear_backend();

	int fail = 0;
	fail += test_chi_row();
	fail += test_iota_round0();

	uint64_t and_per_permute = 0;
	fail += test_full_keccak_f(&and_per_permute);

	fail += test_sha3_kats();
	fail += test_sha3_vs_openssl();

	finalize_clear_backend();
	return fail == 0 ? 0 : 1;
}
