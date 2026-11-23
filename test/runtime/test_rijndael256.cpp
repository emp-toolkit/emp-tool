// crypto/rijndael256.h — Rijndael-256 (256-bit block and key, 14 rounds) on
// the AES round instructions. Read example() first; the rest is verification.
//
// What's in rijndael256.h:
//   R256_KEY                                   15 round keys, as 30 blocks
//   R256_set_encrypt_key(uk, &ek)              schedule of the two-block key uk
//   R256_ecb_encrypt_blks<N>(blks, &ek)        N 256-bit blocks, unrolled
//   R256_ecb_encrypt_blks(blks, n, &ek)        n 256-bit blocks, runtime n
// A 256-bit block is two consecutive blocks: bytes 0..15, then 16..31.

#include "emp-tool/emp-tool.h"

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace emp;
using namespace std;

// ---------- reference ----------

// Byte-oriented Rijndael-256 from the specification. State byte (row r,
// column c) is byte 4c + r; ShiftRows rotates row r left by 0, 1, 3, 4 of the
// eight columns; the Nk = 8 key schedule gives 120 words, 32 bytes per round.
namespace ref {

static uint8_t xtime(uint8_t a) { return (uint8_t)((a << 1) ^ ((a & 0x80) ? 0x1b : 0)); }
static uint8_t gmul(uint8_t a, uint8_t b) {
	uint8_t r = 0;
	for (; b; b >>= 1, a = xtime(a))
		if (b & 1) r ^= a;
	return r;
}

// S-box from its definition: inversion in GF(2^8), then the affine map.
static const uint8_t *sbox() {
	static uint8_t s[256];
	static bool done = false;
	if (!done) {
		for (int x = 0; x < 256; ++x) {
			uint8_t inv = 0;
			for (int y = 1; x && y < 256; ++y)
				if (gmul((uint8_t)x, (uint8_t)y) == 1) { inv = (uint8_t)y; break; }
			uint8_t v = inv;
			for (int i = 1; i <= 4; ++i) v ^= (uint8_t)((inv << i) | (inv >> (8 - i)));
			s[x] = v ^ 0x63;
		}
		done = true;
	}
	return s;
}

static const int kShift[4] = {0, 1, 3, 4};

struct Rijndael256 {
	uint8_t w[15][32];
	explicit Rijndael256(const uint8_t key[32]) {
		const uint8_t *S = sbox();
		uint8_t *words = &w[0][0];
		memcpy(words, key, 32);
		uint8_t rcon = 1;
		for (int i = 8; i < 120; ++i) {
			uint8_t t[4];
			memcpy(t, words + 4 * (i - 1), 4);
			if (i % 8 == 0) {
				const uint8_t t0 = t[0];
				t[0] = S[t[1]] ^ rcon; t[1] = S[t[2]]; t[2] = S[t[3]]; t[3] = S[t0];
				rcon = xtime(rcon);
			} else if (i % 8 == 4) {
				for (int j = 0; j < 4; ++j) t[j] = S[t[j]];
			}
			for (int j = 0; j < 4; ++j) words[4 * i + j] = words[4 * (i - 8) + j] ^ t[j];
		}
	}
	void encrypt(const uint8_t in[32], uint8_t out[32]) const {
		const uint8_t *S = sbox();
		uint8_t s[32], t[32];
		for (int i = 0; i < 32; ++i) s[i] = in[i] ^ w[0][i];
		for (int round = 1; round <= 14; ++round) {
			for (int c = 0; c < 8; ++c)
				for (int r = 0; r < 4; ++r) t[4 * c + r] = S[s[4 * ((c + kShift[r]) % 8) + r]];
			if (round < 14)
				for (int c = 0; c < 8; ++c) {
					const uint8_t *a = t + 4 * c;
					uint8_t m[4];
					for (int r = 0; r < 4; ++r)
						m[r] = gmul(a[r], 2) ^ gmul(a[(r + 1) % 4], 3) ^ a[(r + 2) % 4] ^ a[(r + 3) % 4];
					memcpy(s + 4 * c, m, 4);
				}
			else
				memcpy(s, t, 32);
			for (int i = 0; i < 32; ++i) s[i] ^= w[round][i];
		}
		memcpy(out, s, 32);
	}
};

}  // namespace ref

// ---------- helpers ----------

// Runs f in a child process. True iff the child exits with error()'s status
// and wrote `msg` to stderr, so a crash or a different check does not count.
template <class F>
static bool dies_with(F&& f, const char *msg) {
	int fds[2];
	if (pipe(fds) != 0) return false;
	pid_t pid = fork();
	if (pid == 0) {
		close(fds[0]);
		dup2(fds[1], 2);
		f();
		_exit(0);
	}
	close(fds[1]);
	string err;
	char buf[256];
	ssize_t k;
	while ((k = read(fds[0], buf, sizeof buf)) > 0) err.append(buf, (size_t)k);
	close(fds[0]);
	int st = 0;
	waitpid(pid, &st, 0);
	return WIFEXITED(st) && WEXITSTATUS(st) == 1 && err.find(msg) != string::npos;
}

static void unhex(const char *h, uint8_t *out) {
	auto v = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
	for (int i = 0; h[2 * i]; ++i) out[i] = (uint8_t)(v(h[2 * i]) << 4 | v(h[2 * i + 1]));
}

static void schedule(const uint8_t key[32], R256_KEY *ek) {
	block uk[2];
	memcpy(uk, key, 32);
	R256_set_encrypt_key(uk, ek);
}

// ---------- example: typical usage ----------

static void example() {
	cout << "=== example ===\n";

	// (1) Schedule a 256-bit key (two blocks) and encrypt one 256-bit block
	//     (two blocks) in place.
	block key[2] = {makeBlock(0x0f0e0d0c0b0a0908ULL, 0x0706050403020100ULL),
	                makeBlock(0x1f1e1d1c1b1a1918ULL, 0x1716151413121110ULL)};
	R256_KEY ek;
	R256_set_encrypt_key(key, &ek);
	block msg[2] = {key[0], key[1]};
	R256_ecb_encrypt_blks(msg, 1, &ek);
	cout << "  R256(msg)                    = " << to_hex(msg, 32) << "\n";

	// (2) Compile-time width: four 256-bit blocks (eight blocks) at once.
	block tile[8];
	for (int i = 0; i < 4; ++i) { tile[2 * i] = key[0]; tile[2 * i + 1] = key[1]; }
	R256_ecb_encrypt_blks<4>(tile, &ek);
	cout << "  R256_ecb_encrypt_blks<4>[0]  = " << to_hex(tile, 32) << "\n";
}

// ---------- correctness ----------

// Published known answers (key, plaintext, ciphertext; a plaintext of two
// blocks runs as n = 2). Every one also agrees with py3rijndael, an independent
// implementation of the Rijndael specification.
//   - Daemen & Rijmen, The Design of Rijndael (2nd ed., 2020), Appendix B.3:
//     under the zero key, E(0) and E(E(0)).
//   - Bouncy Castle's RijndaelTest, vector 21 (256-bit block and key).
//   - Saarinen's rij256-rv self-test (github.com/mjosaarinen/rij256-rv): two
//     blocks under the SP 800-38A AES-256 key and plaintext, made with the
//     2001 Rijndael reference code.
//   - py3rijndael's own test (key and ciphertext base64-decoded, plaintext
//     "Mahdi" padded with 0x1b).
static bool check_known_answers() {
	struct Kat { const char *source, *key, *pt, *ct; };
	static const Kat kats[] = {
		{"Design of Rijndael B.3, E(0)",
		 "0000000000000000000000000000000000000000000000000000000000000000",
		 "0000000000000000000000000000000000000000000000000000000000000000",
		 "c6227e7740b7e53b5cb77865278eab0726f62366d9aabad908936123a1fc8af3"},
		{"Design of Rijndael B.3, E(E(0))",
		 "0000000000000000000000000000000000000000000000000000000000000000",
		 "c6227e7740b7e53b5cb77865278eab0726f62366d9aabad908936123a1fc8af3",
		 "9843e807319c32ad1ea3935ef56a2ba96e4bf19c30e47d88a2b97cbbf2e159e7"},
		{"Bouncy Castle RijndaelTest 21",
		 "2b7e151628aed2a6abf7158809cf4f3c762e7160f38b4da56a784d9045190cfe",
		 "3243f6a8885a308d313198a2e03707344a4093822299f31d0082efa98ec4e6c8",
		 "a49406115dfb30a40418aafa4869b7c6a886ff31602a7dd19c889dc64f7e4e7a"},
		{"rij256-rv, two blocks",
		 "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4",
		 "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51"
		 "30c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710",
		 "66a8c19a07243a7cced7aded37e6651f485e9f79d293c439977d90d45a7dc029"
		 "4b5fa2f3c2ca20aaecb912eb116c09bf9b7d109ce69ea39a67be9bb1a3b0d415"},
		{"py3rijndael test",
		 "a814bcb91844201b2bf23afcbee63db94a4615e7d844bd87493b6b2a1688d6d9",
		 "4d616864691b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b",
		 "29cf02def8dff84a4b466813679724593cc9ffa9fb5811d6f29901c83b1c23f1"},
	};
	bool ok = true;
	for (const Kat &v : kats) {
		const int64_t n = (int64_t)strlen(v.pt) / 64;
		uint8_t key[32], pt[64], want[64], got_ref[64];
		unhex(v.key, key); unhex(v.pt, pt); unhex(v.ct, want);
		R256_KEY ek;
		schedule(key, &ek);
		block blk[4];
		memcpy(blk, pt, 32 * n);
		R256_ecb_encrypt_blks(blk, n, &ek);
		const ref::Rijndael256 r(key);
		for (int64_t i = 0; i < n; ++i) r.encrypt(pt + 32 * i, got_ref + 32 * i);
		const bool good = memcmp(blk, want, 32 * n) == 0 && memcmp(got_ref, want, 32 * n) == 0;
		cout << "  [known answer: " << left << setw(36) << string(v.source) + "]" << right
		     << (good ? "OK" : "FAIL") << "\n";
		if (!good) cout << "    got=" << to_hex(blk, 32 * n) << "\n";
		ok &= good;
	}
	return ok;
}

// Published round keys: the Rijndael-256 key-schedule vectors of the FAEST
// implementation's tests (github.com/faest-sign/faest-arch-opt,
// test/test_aes.cpp), also produced by py3rijndael. The reference must give
// them, and so must R256_set_encrypt_key in its own layout: on aarch64 it
// stores P(K_r) for r < 14 (rijndael256.hpp).
static bool check_key_schedule() {
	struct Ks { const char *key; const char *rk[15]; };
	static const Ks vs[] = {
	{"f648818ba4a6656be0cb6e382a5dff72ac1dda96908137478bd536cf4b778ade",
	 {"f648818ba4a6656be0cb6e382a5dff72ac1dda96908137478bd536cf4b778ade",
	  "02369c38a690f953465b976b6c066819fc729f426cf3a805e7269ecaac511414",
	  "d1cc66a9775c9ffa310708915d016088b00e4f86dcfde7833bdb7949978a6d5d",
	  "abf02a21dcacb5dbedabbd4ab0aaddc257a28ea38b5f6920b0841069270e7d34",
	  "080f32edd4a3873639083a7c89a2e7bef0981a0d7bc7732dcb436344ec4d1e70",
	  "fb7d63232fdee41516d6de699f7439d72b0a080350cd7b2e9b8e186a77c3061a",
	  "f512c1d6dacc25c3cc1afbaa536ec27dc6952dfc965856d20dd64eb87a1548a2",
	  "ec40fb0c368cdecffa962565a9f8e71815d4b951838cef838e5aa13bf44fe999",
	  "e85e15b3ded2cb7c2444ee198dbc090148b1b82dcb3d57ae4567f695b1281f0c",
	  "c79eeb7b194c20073d08ce1eb0b4c71faf3c7eed640129432166dfd6904ec0da",
	  "de24bc1bc7689c1cfa6052024ad4951d797454491d757d0a3c13a2dcac5d6206",
	  "fe8ed38a39e64f96c3861d9489528889de7490eec301ede4ff124f38534f2d3e",
	  "a25661679bb02ef158363365d164bbece0377a20233697c4dc24d8fc8f6bf5c2",
	  "76b04414ed006ae5b53659806452e26ca337e270800175b45c25ad48d34e588a",
	  "14da3a72f9da50974cec091728beeb7b97990b5117987ee54bbdd3ad98f38b27"}},
	{"1fe7a9010b3341c2bd2b4acec46edf287a43b9b21175306c76a81a5789932247",
	 {"1fe7a9010b3341c2bd2b4acec46edf287a43b9b21175306c76a81a5789932247",
	  "c27409a6c9474864746c02aab002dd829d3478a18c4148cdfae9529a737a70dd",
	  "1a25c829d362804da70e82e7170c5f656dcab7ece18bff211b62adbb6818dd66",
	  "b3e4fb6c60867b21c788f9c6d084a6a31d9593e6fc1e6cc7e77cc17c8f641c1a",
	  "f878591f98fe223e5f76dbf88ff27d5b6e1c6cdf92020018757ec164fa1add7e",
	  "4ab9aa32d247880c8d3153f402c32eaf19325da68b305dbefe4e9cda045441a4",
	  "4a3ae3c0987d6bcc154c3838178f1697e9411a2e627147909c3fdb4a986b9aee",
	  "7582cb86edffa04af8b39872ef3c8ee536aa03f754db4467c8e49f2d508f05c3",
	  "86e9e5d56b16459f93a5dded7c9953082644eec7729faaa0ba7b358deaf4304e",
	  "22edca5249fb8fcdda5e5220a6c70128028292f3701d3853ca660dde20923d90",
	  "5bcaaae512312528c86f77086ea876209d40aa44ed5d9217273b9fc907a9a259",
	  "e4f06120f6c144083eae330050064520ce2fc4f3237256e40449c92d03e06b74",
	  "dd8ff35b2b4eb75315e0845345e6c173a0a1bc7c83d3ea98879a23b5847a48c1",
	  "acdd8b0487933c579273b804d7957977ae8b0a892d58e011aac2c3a42eb88b65",
	  "8de0c6350a73fa62980042664f953b112aa1e80b07f9081aad3bcbbe838340db"}},
	{"3081cd277bcd1e3763ea0bf5ee5974c3790f2b56ed732a1a1131be177dea4261",
	 {"3081cd277bcd1e3763ea0bf5ee5974c3790f2b56ed732a1a1131be177dea4261",
	  "b6ad22d8cd603cefae8a371a40d343d9706931639d1a1b798c2ba56ef1c1e70f",
	  "cc39547901596896afd35f8cef001c55af0aad9f3210b6e6be3b13884ffaf487",
	  "e58643fde4df2b6b4b0c74e7a40c68b2e6f4e8a8d4e45e4e6adf4dc62525b941",
	  "d2d0c0c2360feba97d039f4ed90ff7fcd38280180766de566db99390489c2ad1",
	  "1c35fe902a3a153957398a778e367d8bca877f25cde1a173a05832e3e8c41832",
	  "2098dd0b0aa2c8325d9b4245d3ad3fceac120aae61f3abddc1ab993e296f810c",
	  "c89423aec236eb9c9fada9d94c00961785719a5ee48231832529a8bd0c4629b1",
	  "1231eb50d00700cc4faaa91503aa3f02feddef291a5fdeaa3f76761733305fa6",
	  "0dfecf93ddf9cf5f9253664a91f959487f44247b651bfad15a6d8cc6695dd360",
	  "77981f6aaa61d0353832b67fa9cbef37ac5bfbe1c9400130932d8df6fa705e96",
	  "4ac08f47e0a15f72d893e90d7158063a0f319461c6719551555c18a7af2c4631",
	  "e39a483e033b174cdba8fe41aaf0f87ba3bdd54065cc4011309058b69fbc1e87",
	  "2de85fe52ed348a9f57bb6e85f8b4e936c80fa9c094cba8d39dce23ba660fcbc",
	  "b0583ac19e8b72686bf0c480347b8a1374a184e17ded3e6c4431dc57e25120eb"}},
	};
	bool ok = true;
	for (const Ks &v : vs) {
		uint8_t key[32], want[15][32];
		unhex(v.key, key);
		for (int r = 0; r < 15; ++r) unhex(v.rk[r], want[r]);
		ok &= memcmp(ref::Rijndael256(key).w, want, sizeof want) == 0;
		R256_KEY ek;
		schedule(key, &ek);
		for (int r = 0; r < 15; ++r) {
			uint8_t expect[32];
			memcpy(expect, want[r], 32);
#if defined(__aarch64__)
			if (r < 14)
				for (int i = 0; i < 32; ++i) expect[i] = want[r][detail::r256_perm[i]];
#endif
			ok &= memcmp(ek.rd_key + 2 * r, expect, 32) == 0;
		}
	}
	cout << "  [key schedule: FAEST round-key vectors]          " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

// buf holds n 256-bit blocks after one guard block pair; the guards and the
// pair after the run must come back untouched.
struct Run {
	vector<block> buf;
	vector<uint8_t> want;
	int64_t n;
	Run(PRG &prg, const uint8_t key[32], int64_t n_) : buf(2 * n_ + 4), want(32 * n_), n(n_) {
		prg.random_block(buf.data(), (int64_t)buf.size());
		const ref::Rijndael256 r(key);
		const uint8_t *in = reinterpret_cast<const uint8_t *>(buf.data() + 2);
		for (int64_t i = 0; i < n; ++i) r.encrypt(in + 32 * i, want.data() + 32 * i);
	}
	block *data() { return buf.data() + 2; }
	bool matches(const vector<block> &before) const {
		const bool guards = memcmp(buf.data(), before.data(), 32) == 0 &&
		                    memcmp(buf.data() + 2 + 2 * n, before.data() + 2 + 2 * n, 32) == 0;
		// An empty `want` may have a null data(), which memcmp must not get.
		return guards && (n == 0 || memcmp(buf.data() + 2, want.data(), want.size()) == 0);
	}
};

static bool check_runtime_widths() {
	PRG prg;
	bool ok = true;
	vector<int64_t> ns;
	for (int64_t n = 0; n <= 40; ++n) ns.push_back(n);
	for (int64_t n : {63, 64, 65, 100, 257}) ns.push_back(n);
	for (int64_t n : ns) {
		uint8_t key[32];
		prg.random_data_unaligned(key, 32);
		R256_KEY ek;
		schedule(key, &ek);
		Run run(prg, key, n);
		const vector<block> before = run.buf;
		R256_ecb_encrypt_blks(run.data(), n, &ek);
		if (!run.matches(before)) {
			cout << "  [runtime n=" << n << "] FAIL\n";
			ok = false;
		}
	}
	cout << "  [runtime n = 0..40, 63, 64, 65, 100, 257 vs reference] " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

template <int N>
static bool check_template_width(PRG &prg) {
	uint8_t key[32];
	prg.random_data_unaligned(key, 32);
	R256_KEY ek;
	schedule(key, &ek);
	Run run(prg, key, N);
	const vector<block> before = run.buf;
	R256_ecb_encrypt_blks<N>(run.data(), &ek);
	const bool ok = run.matches(before);
	if (!ok) cout << "  [R256_ecb_encrypt_blks<" << N << ">] FAIL\n";
	return ok;
}

template <int... Ns>
static bool check_template_widths(std::integer_sequence<int, Ns...>) {
	PRG prg;
	const bool ok = (check_template_width<Ns>(prg) & ...);
	cout << "  [R256_ecb_encrypt_blks<N>, N = 0..17, 31..33 vs reference] " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

// The kernels' tables, derived again from the two ShiftRows. In gather form
// ((G x)[i] = x[g[i]]), SR8 is the Rijndael-256 ShiftRows and SR4 the AES one
// on each half; AESENC applies SR4 to its input, so P must satisfy
// P[SR4[i]] = SR8[i]. The split form reads P one half at a time.
static bool check_permutation_tables() {
	uint8_t sr8[32], sr4[32], perm[32];
	for (int c = 0; c < 8; ++c)
		for (int r = 0; r < 4; ++r) sr8[4 * c + r] = (uint8_t)(4 * ((c + ref::kShift[r]) % 8) + r);
	for (int h = 0; h < 2; ++h)
		for (int j = 0; j < 4; ++j)
			for (int r = 0; r < 4; ++r) sr4[16 * h + 4 * j + r] = (uint8_t)(16 * h + 4 * ((j + r) % 4) + r);
	for (int i = 0; i < 32; ++i) perm[sr4[i]] = sr8[i];
	bool ok = memcmp(perm, detail::r256_perm, 32) == 0;
	// Half h, byte i: source byte perm[16h + i] = 16 * (other half?) + position.
	for (int h = 0; h < 2; ++h)
		for (int i = 0; i < 16; ++i) {
			const int src = perm[16 * h + i], pos = src % 16;
			const bool other = src / 16 != h;
			ok &= detail::r256_shuffle[i] == pos;
			ok &= (detail::r256_blend_mask[pos] == 0xff) == other;
		}
	cout << "  [permutation tables from ShiftRows]      " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_contracts() {
	R256_KEY ek;
	block key[2] = {zero_block, zero_block};
	R256_set_encrypt_key(key, &ek);
	block blk[2] = {zero_block, zero_block};
	const bool neg = dies_with([&] { R256_ecb_encrypt_blks(blk, -1, &ek); },
	                           "R256_ecb_encrypt_blks: negative block count");
	const bool huge = dies_with([&] {
		R256_ecb_encrypt_blks(blk, numeric_limits<int64_t>::max() / 32 + 1, &ek);
	}, "R256_ecb_encrypt_blks: block count overflows int64_t bytes");
	const bool ok = neg && huge;
	cout << "  [negative / overflowing n rejected]      " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	bool ok = true;
	ok &= check_known_answers();
	ok &= check_key_schedule();
	ok &= check_runtime_widths();
	ok &= check_template_widths(std::integer_sequence<int, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
	                                                    13, 14, 15, 16, 17, 31, 32, 33>{});
	ok &= check_permutation_tables();
	ok &= check_contracts();
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
