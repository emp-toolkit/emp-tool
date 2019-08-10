/********************************************************************/
/* Copyright(c) 2014, Intel Corp.                                   */
/* Developers and authors: Shay Gueron (1) (2)                      */
/* (1) University of Haifa, Israel                                  */
/* (2) Intel, Israel                                                */
/* IPG, Architecture, Israel Development Center, Haifa, Israel      */
/********************************************************************/


#ifndef LIBGARBLE_AES_OPT_H
#define LIBGARBLE_AES_OPT_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wmmintrin.h>

#if defined(__INTEL_COMPILER)
# include <ia32intrin.h> 
#elif defined(__GNUC__)
	# include <emmintrin.h>
# include <smmintrin.h>
#endif

#include "emp-tool/utils/block.h"

#if !defined (ALIGN16)
#if defined (__GNUC__)
#  define ALIGN16  __attribute__  ( (aligned (16)))
# else
#  define ALIGN16 __declspec (align (16))
# endif
#endif

namespace emp {

typedef struct KEY_SCHEDULE
{
   ALIGN16 unsigned char KEY[16*15];
   unsigned int nr;
} ROUND_KEYS; 


#define KS_BLOCK(t, reg, reg2) {globAux=_mm_slli_epi64(reg, 32);\
								reg=_mm_xor_si128(globAux, reg);\
								globAux=_mm_shuffle_epi8(reg, con3);\
								reg=_mm_xor_si128(globAux, reg);\
								reg=_mm_xor_si128(reg2, reg);\
								}

#define KS_round(i) { x2 =_mm_shuffle_epi8(keyA, mask); \
	keyA_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(0, keyA, keyA_aux);\
	x2 =_mm_shuffle_epi8(keyB, mask); \
	keyB_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(1, keyB, keyB_aux);\
	con=_mm_slli_epi32(con, 1);\
	_mm_storeu_si128((__m128i *)(keys[0].KEY+i*16), keyA);\
	_mm_storeu_si128((__m128i *)(keys[1].KEY+i*16), keyB);	\
	}

#define KS_round_last(i) { x2 =_mm_shuffle_epi8(keyA, mask); \
	keyA_aux=_mm_aesenclast_si128 (x2, con); \
	x2 =_mm_shuffle_epi8(keyB, mask); \
	keyB_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(0, keyA, keyA_aux);\
	KS_BLOCK(1, keyB, keyB_aux);\
	_mm_storeu_si128((__m128i *)(keys[0].KEY+i*16), keyA);\
	_mm_storeu_si128((__m128i *)(keys[1].KEY+i*16), keyB);	\
	}

#define READ_KEYS(i) {keyA = _mm_loadu_si128((__m128i const*)(keys[0].KEY+i*16));\
	keyB = _mm_loadu_si128((__m128i const*)(keys[1].KEY+i*16));\
	}
		
#define ENC_round_22(i) {block1=_mm_aesenc_si128(block1, (*(__m128i const*)(keys[0].KEY+i*16))); \
	block2=_mm_aesenc_si128(block2, (*(__m128i const*)(keys[1].KEY+i*16))); \
}	
	
#define ENC_round_22_last(i) {block1=_mm_aesenclast_si128(block1, (*(__m128i const*)(keys[0].KEY+i*16))); \
	block2=_mm_aesenclast_si128(block2, (*(__m128i const*)(keys[1].KEY+i*16))); \
}

#define ENC_round_24(i) {block1=_mm_aesenc_si128(block1, (*(__m128i const*)(keys[0].KEY+i*16))); \
	block2=_mm_aesenc_si128(block2, (*(__m128i const*)(keys[0].KEY+i*16))); \
	block3=_mm_aesenc_si128(block3, (*(__m128i const*)(keys[1].KEY+i*16))); \
	block4=_mm_aesenc_si128(block4, (*(__m128i const*)(keys[1].KEY+i*16))); \
}	
	
#define ENC_round_24_last(i) {block1=_mm_aesenclast_si128(block1, (*(__m128i const*)(keys[0].KEY+i*16))); \
	block2=_mm_aesenclast_si128(block2, (*(__m128i const*)(keys[0].KEY+i*16))); \
	block3=_mm_aesenclast_si128(block3, (*(__m128i const*)(keys[1].KEY+i*16))); \
	block4=_mm_aesenclast_si128(block4, (*(__m128i const*)(keys[1].KEY+i*16))); \
}

static block sigma(block a) {
	return xorBlocks(_mm_shuffle_epi32(a, 78), _mm_and_si128(a, _mm_set_epi64x(0xFFFFFFFFFFFFFFFF, 0x00)));
}

/*
void print_blocks(block *b, int nblks) {
	for(int i = 0; i < nblks; ++i) {
		unsigned char* a = (unsigned char*)(&b[i]);
		for(int j = 0; j < 16; ++j)
			printf("%02X", a[j]);
		printf("\n");
	}
}	

void print_round_key(ROUND_KEYS *r_key) {
	printf("\nround key\n");
	unsigned char* key = r_key->KEY;
	for(int i = 0; i < 11; ++i) {
		for(int j = 0; j < 16; ++j)
			printf("%02X", key[i*16+j]);
		printf("\n");
	}
}
*/

static inline void AES_ks2(block* user_key, ROUND_KEYS *KEYS) {
	//ROUND_KEYS KEYS[2];
	unsigned char *first_key = (unsigned char*)user_key;

	ROUND_KEYS *keys = KEYS;
	register __m128i keyA, keyB, con, mask, x2, keyA_aux, keyB_aux, globAux;
	int _con1[4]={1,1,1,1};
	int _con2[4]={0x1b,0x1b,0x1b,0x1b};
	int _mask[4]={0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d};
	unsigned int _con3[4]={0x0ffffffff, 0x0ffffffff, 0x07060504, 0x07060504};
	__m128i con3=_mm_loadu_si128((__m128i const*)_con3);

	keys[0].nr=10;
	keys[1].nr=10;

	keyA = _mm_loadu_si128((__m128i const*)(first_key));	
	keyB = _mm_loadu_si128((__m128i const*)(first_key+16));	

	_mm_storeu_si128((__m128i *)keys[0].KEY, keyA);	
	_mm_storeu_si128((__m128i *)keys[1].KEY, keyB);	
		
	con = _mm_loadu_si128((__m128i const*)_con1);	
	mask = _mm_loadu_si128((__m128i const*)_mask);	
	
	KS_round(1)
	KS_round(2)
	KS_round(3)
	KS_round(4)
	KS_round(5)
	KS_round(6)
	KS_round(7)
	KS_round(8)

	con = _mm_loadu_si128((__m128i const*)_con2);			

	KS_round(9)
	KS_round_last(10)
}

static inline void AES_ecb_ks2_enc2(block* user_key, block *plaintext, block *ciphertext) {
	ROUND_KEYS KEYS[2];
	unsigned char *first_key = (unsigned char*)user_key;
	unsigned char* PT = (unsigned char*)plaintext;
	unsigned char* CT = (unsigned char*)ciphertext;
	

	// key schedule

	ROUND_KEYS *keys = KEYS;
	register __m128i keyA, keyB, con, mask, x2, keyA_aux, keyB_aux, globAux;
	int _con1[4]={1,1,1,1};
	int _con2[4]={0x1b,0x1b,0x1b,0x1b};
	int _mask[4]={0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d};
	unsigned int _con3[4]={0x0ffffffff, 0x0ffffffff, 0x07060504, 0x07060504};
	__m128i con3=_mm_loadu_si128((__m128i const*)_con3);
	
	keys[0].nr=10;
	keys[1].nr=10;

	keyA = _mm_loadu_si128((__m128i const*)(first_key));	
	keyB = _mm_loadu_si128((__m128i const*)(first_key+16));	

	_mm_storeu_si128((__m128i *)keys[0].KEY, keyA);	
	_mm_storeu_si128((__m128i *)keys[1].KEY, keyB);	
		
	con = _mm_loadu_si128((__m128i const*)_con1);	
	mask = _mm_loadu_si128((__m128i const*)_mask);	
	
	KS_round(1)
	KS_round(2)
	KS_round(3)
	KS_round(4)
	KS_round(5)
	KS_round(6)
	KS_round(7)
	KS_round(8)

	con = _mm_loadu_si128((__m128i const*)_con2);			

	KS_round(9)
	KS_round_last(10)

	//print_round_key(KEYS);
	//print_round_key(KEYS+1);


	// encryption 

	keys = KEYS;
	
	register __m128i block1 = _mm_loadu_si128((__m128i const*)(0*16+PT));	
	register __m128i block2 = _mm_loadu_si128((__m128i const*)(1*16+PT));	

	READ_KEYS(0)
	
	block1 = _mm_xor_si128(keyA, block1);
	block2 = _mm_xor_si128(keyB, block2);

	ENC_round_22(1)
	ENC_round_22(2)
	ENC_round_22(3)
	ENC_round_22(4)
	ENC_round_22(5)
	ENC_round_22(6)
	ENC_round_22(7)
	ENC_round_22(8)
	ENC_round_22(9)
	ENC_round_22_last(10)
	
	_mm_storeu_si128((__m128i *)(CT+0*16), block1);	
	_mm_storeu_si128((__m128i *)(CT+1*16), block2);	
	
	//print_blocks(ciphertext, 2);
}

static inline void AES_ecb_ks2_enc4(block* user_key, block *plaintext, block *ciphertext) {
	ROUND_KEYS KEYS[2];
	unsigned char *first_key = (unsigned char*)user_key;
	unsigned char* PT = (unsigned char*)plaintext;
	unsigned char* CT = (unsigned char*)ciphertext;
	

	// key schedule

	ROUND_KEYS *keys = KEYS;
	register __m128i keyA, keyB, con, mask, x2, keyA_aux, keyB_aux, globAux;
	int _con1[4]={1,1,1,1};
	int _con2[4]={0x1b,0x1b,0x1b,0x1b};
	int _mask[4]={0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d};
	unsigned int _con3[4]={0x0ffffffff, 0x0ffffffff, 0x07060504, 0x07060504};
	__m128i con3=_mm_loadu_si128((__m128i const*)_con3);

	
	keys[0].nr=10;
	keys[1].nr=10;

	keyA = _mm_loadu_si128((__m128i const*)(first_key));	
	keyB = _mm_loadu_si128((__m128i const*)(first_key+16));	

	_mm_storeu_si128((__m128i *)keys[0].KEY, keyA);	
	_mm_storeu_si128((__m128i *)keys[1].KEY, keyB);	
		
	con = _mm_loadu_si128((__m128i const*)_con1);	
	mask = _mm_loadu_si128((__m128i const*)_mask);	
	
	KS_round(1)
	KS_round(2)
	KS_round(3)
	KS_round(4)
	KS_round(5)
	KS_round(6)
	KS_round(7)
	KS_round(8)

	con = _mm_loadu_si128((__m128i const*)_con2);			

	KS_round(9)
	KS_round_last(10)

	//print_round_key(KEYS);
	//print_round_key(KEYS+1);


	// encryption 

	keys = KEYS;
	
	register __m128i block1 = _mm_loadu_si128((__m128i const*)(0*16+PT));	
	register __m128i block2 = _mm_loadu_si128((__m128i const*)(1*16+PT));	
	register __m128i block3 = _mm_loadu_si128((__m128i const*)(2*16+PT));	
	register __m128i block4 = _mm_loadu_si128((__m128i const*)(3*16+PT));	
		
	READ_KEYS(0)
	
	block1 = _mm_xor_si128(keyA, block1);
	block2 = _mm_xor_si128(keyA, block2);
	block3 = _mm_xor_si128(keyB, block3);
	block4 = _mm_xor_si128(keyB, block4);
	
	ENC_round_24(1)
	ENC_round_24(2)
	ENC_round_24(3)
	ENC_round_24(4)
	ENC_round_24(5)
	ENC_round_24(6)
	ENC_round_24(7)
	ENC_round_24(8)
	ENC_round_24(9)
	ENC_round_24_last(10)
	
	_mm_storeu_si128((__m128i *)(CT+0*16), block1);	
	_mm_storeu_si128((__m128i *)(CT+1*16), block2);	
	_mm_storeu_si128((__m128i *)(CT+2*16), block3);	
	_mm_storeu_si128((__m128i *)(CT+3*16), block4);	
	
	//print_blocks(ciphertext, 4);
}

static inline void AES_ecb_ccr_ks2_enc2(block random, uint64_t idx, block *plaintext, block *ciphertext) {
	block user_key[2];
	user_key[0] = xorBlocks(makeBlock(2 * idx, (uint64_t)0), random);
	user_key[1] = xorBlocks(makeBlock(2 * idx + 1, (uint64_t)0), random);
	AES_ecb_ks2_enc2(user_key, plaintext, ciphertext);
}

static inline void AES_ecb_ccr_ks2_enc4(block random, uint64_t idx, block *plaintext, block *ciphertext) {
	block user_key[2];
	user_key[0] = xorBlocks(makeBlock(2 * idx, (uint64_t)0), random);
	user_key[1] = xorBlocks(makeBlock(2 * idx + 1, (uint64_t)0), random);
	AES_ecb_ks2_enc4(user_key, plaintext, ciphertext);
}

}
#endif
