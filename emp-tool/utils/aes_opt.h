#ifndef EMP_AES_OPT_KS_H__
#define EMP_AES_OPT_KS_H__

#include "emp-tool/utils/aes.h"

namespace emp {
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
 * AES key scheduling for 8 keys
 * [REF] Implementation of "Fast Garbling of Circuits Under Standard Assumptions"
 * https://eprint.iacr.org/2015/751.pdf
 */
template<int NumKeys>
static inline void AES_opt_key_schedule(block* user_key, AES_KEY *keys) {
	block con = _mm_set_epi32(1,1,1,1);
	block con2 = _mm_set_epi32(0x1b,0x1b,0x1b,0x1b);
	block con3 = _mm_set_epi32(0x07060504,0x07060504,0x0ffffffff,0x0ffffffff);
	block mask = _mm_set_epi32(0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d);

	for(int i = 0; i < NumKeys; ++i) {
		keys[i].rounds=10;
		keys[i].rd_key[0] = user_key[i];
	}

	ks_rounds<NumKeys>(keys, con, con3, mask, 1);
	con=_mm_slli_epi32(con, 1);
	ks_rounds<NumKeys>(keys, con, con3, mask, 2);
	con=_mm_slli_epi32(con, 1);
	ks_rounds<NumKeys>(keys, con, con3, mask, 3);
	con=_mm_slli_epi32(con, 1);
	ks_rounds<NumKeys>(keys, con, con3, mask, 4);
	con=_mm_slli_epi32(con, 1);
	ks_rounds<NumKeys>(keys, con, con3, mask, 5);
	con=_mm_slli_epi32(con, 1);
	ks_rounds<NumKeys>(keys, con, con3, mask, 6);
	con=_mm_slli_epi32(con, 1);
	ks_rounds<NumKeys>(keys, con, con3, mask, 7);
	con=_mm_slli_epi32(con, 1);
	ks_rounds<NumKeys>(keys, con, con3, mask, 8);
	ks_rounds<NumKeys>(keys, con2, con3, mask, 9);
	con2=_mm_slli_epi32(con2, 1);
	ks_rounds<NumKeys>(keys, con2, con3, mask, 10);
}

/*
 * With numKeys keys, use each key to encrypt numEncs blocks.
 */
#ifdef __x86_64__
template<int numKeys, int numEncs>
static inline void ParaEnc(block *blks, AES_KEY *keys) {
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
}
#elif __aarch64__
template<int numKeys, int numEncs>
static inline void ParaEnc(block *_blks, AES_KEY *keys) {
	uint8x16_t * first = (uint8x16_t*)(_blks);

	for (unsigned int r = 0; r < 9; ++r) { 
		auto blks = first;
		for(size_t i = 0; i < numKeys; ++i) {
			uint8x16_t K = vreinterpretq_u8_m128i(keys[i].rd_key[r]);
			for(size_t j = 0; j < numEncs; ++j, ++blks)
			   *blks = vaesmcq_u8(vaeseq_u8(*blks, K));
		}
	}
	
	auto blks = first;
	for(size_t i = 0; i < numKeys; ++i) {
		uint8x16_t K = vreinterpretq_u8_m128i(keys[i].rd_key[9]);
		uint8x16_t K2 = vreinterpretq_u8_m128i(keys[i].rd_key[10]);
		for(size_t j = 0; j < numEncs; ++j, ++blks)
			*blks = vaeseq_u8(*blks, K) ^ K2;
	}
}
#endif

}
#endif
