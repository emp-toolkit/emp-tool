#ifndef EMP_AES_H
#define EMP_AES_H

#include "emp-tool/core/block.h"
#include <cassert>
#include <cstdint>

namespace emp {

typedef struct { block rd_key[11]; } AES_KEY;

#define EMP_AES_ASSERT_ALIGNED(p) \
	assert((reinterpret_cast<uintptr_t>(p) & (alignof(block) - 1)) == 0 \
	       && "pointer must be 16-byte (block) aligned")

// On x86_64, ParaEnc gets a VAES+AVX-512 path that runs 4 AES blocks per
// vaesenc when the build target supports it. The function-level target
// attribute widens the kernel's allowed ISA so the compiler can emit
// zmm/ymm AES forms even when the rest of the TU was compiled with
// "aes,sse2" only. A user building with -march=native on a VAES-capable
// host will have __VAES__ and __AVX512F__ defined globally and pick this
// path automatically.
#if defined(__x86_64__) && defined(__VAES__) && defined(__AVX512F__)
	#define EMP_AES_TARGET_ATTR __attribute__((target("vaes,avx512f,aes,sse2")))
	#define EMP_AES_HAS_VAES512 1
#elif defined(__x86_64__)
	#define EMP_AES_TARGET_ATTR __attribute__((target("aes,sse2")))
	#define EMP_AES_HAS_VAES512 0
#else
	#define EMP_AES_TARGET_ATTR
	#define EMP_AES_HAS_VAES512 0
#endif

template<int NumKeys>
static inline void ks_rounds(AES_KEY * keys, block con, block con3, block mask, int r) {
	for (int i = 0; i < NumKeys; ++i) {
		block key = keys[i].rd_key[r-1];
		block x2 =_mm_shuffle_epi8(key, mask);
		block aux = _mm_aesenclast_si128 (x2, con);

		block globAux=_mm_slli_epi64(key, 32);
		key=_mm_xor_si128(globAux, key);
		globAux=_mm_shuffle_epi8(key, con3);
		key=_mm_xor_si128(globAux, key);
		keys[i].rd_key[r] = _mm_xor_si128(aux, key);
	}
}

/*
 * AES key scheduling for NumKeys keys.
 * [REF] "Fast Garbling of Circuits Under Standard Assumptions"
 * https://eprint.iacr.org/2015/751.pdf
 */
template<int NumKeys>
static inline void AES_opt_key_schedule(const block* user_key, AES_KEY *keys) {
	EMP_AES_ASSERT_ALIGNED(user_key);
	EMP_AES_ASSERT_ALIGNED(keys);
	block con = _mm_set_epi32(1,1,1,1);
	block con2 = _mm_set_epi32(0x1b,0x1b,0x1b,0x1b);
	block con3 = _mm_set_epi32(0x07060504,0x07060504,0x0ffffffff,0x0ffffffff);
	block mask = _mm_set_epi32(0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d);

	for(int i = 0; i < NumKeys; ++i)
		keys[i].rd_key[0] = user_key[i];

	for (int r = 1; r <= 8; ++r) {
		ks_rounds<NumKeys>(keys, con, con3, mask, r);
		con = _mm_slli_epi32(con, 1);
	}
	ks_rounds<NumKeys>(keys, con2, con3, mask, 9);
	con2 = _mm_slli_epi32(con2, 1);
	ks_rounds<NumKeys>(keys, con2, con3, mask, 10);
}

/*
 * With numKeys keys, use each key to encrypt numEncs blocks.
 *
 * Performance: the kernel is fully unrolled, so all K*N blocks and all K*11
 * round keys are expected to live in SIMD registers simultaneously. Targets
 * with 32 SIMD registers (modern ARMv8, AVX-512 x86) tolerate K*N up to
 * about 16 before the round keys plus working state exceed the register
 * file and the kernel starts spilling; throughput then drops 2–3×. Sweet
 * spot is K*N ≤ 16 — for larger work, dispatch through the runtime
 * ParaEnc(...) below, which tiles into {<1,8>,<1,4>,<1,2>,<1,1>}.
 *
 * On x86_64 with VAES+AVX-512 we use 4-block zmm tiles via
 * _mm512_aesenc_epi128 for ~2× over scalar AES-NI.
 */
#ifdef __x86_64__
template<int numKeys, int numEncs>
EMP_AES_TARGET_ATTR
static inline void ParaEnc(block *blks, const AES_KEY *keys) {
	EMP_AES_ASSERT_ALIGNED(blks);
	EMP_AES_ASSERT_ALIGNED(keys);
#if EMP_AES_HAS_VAES512
	// Per-key serial: each key's numEncs blocks form an independent stream.
	// Within a stream, partition into 4-block zmm tiles + an optional
	// 2-block ymm + 1-block scalar tail.
	constexpr int FULL = numEncs / 4;
	constexpr int REM  = numEncs % 4;
	for (size_t k = 0; k < numKeys; ++k) {
		block * const p = blks + k * (size_t)numEncs;
		const AES_KEY * const kk = keys + k;

		// Broadcast each 128-bit round key to all 4 lanes of a zmm.
		__m512i rk[11];
		for (int r = 0; r < 11; ++r)
			rk[r] = _mm512_broadcast_i32x4(kk->rd_key[r]);

		// Full 4-block zmm tiles.
		for (int t = 0; t < FULL; ++t) {
			__m512i x = _mm512_loadu_si512((const __m512i *)(p + t * 4));
			x = _mm512_xor_si512(x, rk[0]);
			for (int r = 1; r < 10; ++r)
				x = _mm512_aesenc_epi128(x, rk[r]);
			x = _mm512_aesenclast_epi128(x, rk[10]);
			_mm512_storeu_si512((__m512i *)(p + t * 4), x);
		}

		// Tail. REM is a compile-time constant; only one branch survives.
		block * tail = p + FULL * 4;
		if (REM >= 2) {
			__m256i rk2[11];
			for (int r = 0; r < 11; ++r)
				rk2[r] = _mm256_broadcastsi128_si256(kk->rd_key[r]);
			__m256i x = _mm256_loadu_si256((const __m256i *)tail);
			x = _mm256_xor_si256(x, rk2[0]);
			for (int r = 1; r < 10; ++r)
				x = _mm256_aesenc_epi128(x, rk2[r]);
			x = _mm256_aesenclast_epi128(x, rk2[10]);
			_mm256_storeu_si256((__m256i *)tail, x);
			tail += 2;
		}
		if (REM == 1 || REM == 3) {
			block x = *tail;
			x = x ^ kk->rd_key[0];
			for (int r = 1; r < 10; ++r)
				x = _mm_aesenc_si128(x, kk->rd_key[r]);
			x = _mm_aesenclast_si128(x, kk->rd_key[10]);
			*tail = x;
		}
	}
#else
	// AES-NI fallback. Round-major outer loop interleaves all blocks at the
	// same round depth, exposing ILP across the AESENC pipeline.
	block * first = blks;
	for(size_t i = 0; i < numKeys; ++i) {
		block K = keys[i].rd_key[0];
		for(size_t j = 0; j < numEncs; ++j) {
			*blks = *blks ^ K;
			++blks;
		}
	}

	for (unsigned int r = 1; r < 10; ++r) {
		blks = first;
		for(size_t i = 0; i < numKeys; ++i) {
			block K = keys[i].rd_key[r];
			for(size_t j = 0; j < numEncs; ++j) {
				*blks = _mm_aesenc_si128(*blks, K);
				++blks;
			}
		}
	}

	blks = first;
	for(size_t i = 0; i < numKeys; ++i) {
		block K = keys[i].rd_key[10];
		for(size_t j = 0; j < numEncs; ++j) {
			*blks = _mm_aesenclast_si128(*blks, K);
			++blks;
		}
	}
#endif
}
#elif __aarch64__
template<int numKeys, int numEncs>
static inline void ParaEnc(block *blks, const AES_KEY *keys) {
	EMP_AES_ASSERT_ALIGNED(blks);
	EMP_AES_ASSERT_ALIGNED(keys);
	uint8x16_t * first = (uint8x16_t*)(blks);

	for (unsigned int r = 0; r < 9; ++r) {
		auto cur = first;
		for(size_t i = 0; i < numKeys; ++i) {
			uint8x16_t K = vreinterpretq_u8_m128i(keys[i].rd_key[r]);
			for(size_t j = 0; j < numEncs; ++j, ++cur)
			   *cur = vaesmcq_u8(vaeseq_u8(*cur, K));
		}
	}

	auto cur = first;
	for(size_t i = 0; i < numKeys; ++i) {
		uint8x16_t K = vreinterpretq_u8_m128i(keys[i].rd_key[9]);
		uint8x16_t K2 = vreinterpretq_u8_m128i(keys[i].rd_key[10]);
		for(size_t j = 0; j < numEncs; ++j, ++cur)
			*cur = vaeseq_u8(*cur, K) ^ K2;
	}
}
#endif

/*
 * Runtime-sized companion to ParaEnc<K, N>: encrypts N blocks under each
 * of K keys with the same K-major layout (blks[k*N..k*N+N) under keys[k]).
 * Dispatches per key into compile-time tiles {8, 4, 2, 1} so each tile
 * still goes through the unrolled, register-resident templated kernel.
 *
 * Cross-key ILP is not exploited here — preserving K-major layout while
 * tiling on N would require interleaved layouts. Hot callers that know
 * (K, N) at compile time should use ParaEnc<K, N> directly.
 */
EMP_AES_TARGET_ATTR
inline void ParaEnc(block *blks, const AES_KEY *keys, int K, int N) {
	EMP_AES_ASSERT_ALIGNED(blks);
	EMP_AES_ASSERT_ALIGNED(keys);
	for (int k = 0; k < K; ++k) {
		block *p = blks + (size_t)k * N;
		int n = N;
		while (n >= 8) { ParaEnc<1, 8>(p, keys + k); p += 8; n -= 8; }
		if (n >= 4)    { ParaEnc<1, 4>(p, keys + k); p += 4; n -= 4; }
		if (n >= 2)    { ParaEnc<1, 2>(p, keys + k); p += 2; n -= 2; }
		if (n >= 1)      ParaEnc<1, 1>(p, keys + k);
	}
}

// --- Single-key encrypt/decrypt wrappers ---

inline void AES_set_encrypt_key(const block& userkey, AES_KEY *key) {
	AES_opt_key_schedule<1>(&userkey, key);
}

template<int N>
inline void AES_ecb_encrypt_blks(block *blks, const AES_KEY *key) {
	ParaEnc<1, N>(blks, key);
}

inline void AES_ecb_encrypt_blks(block *blks, unsigned nblks, const AES_KEY *key) {
	ParaEnc(blks, key, 1, (int)nblks);
}

}  // namespace emp
#endif
