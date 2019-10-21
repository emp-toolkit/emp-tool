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

// number of batched key schedule
#define KS_BATCH_N 8

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

#define KS_round_2(i) { x2 =_mm_shuffle_epi8(keyA, mask); \
	keyA_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(0, keyA, keyA_aux);\
	x2 =_mm_shuffle_epi8(keyB, mask); \
	keyB_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(1, keyB, keyB_aux);\
	con=_mm_slli_epi32(con, 1);\
	_mm_storeu_si128((__m128i *)(keys[0].KEY+i*16), keyA);\
	_mm_storeu_si128((__m128i *)(keys[1].KEY+i*16), keyB);	\
	}

#define KS_round_2_last(i) { x2 =_mm_shuffle_epi8(keyA, mask); \
	keyA_aux=_mm_aesenclast_si128 (x2, con); \
	x2 =_mm_shuffle_epi8(keyB, mask); \
	keyB_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(0, keyA, keyA_aux);\
	KS_BLOCK(1, keyB, keyB_aux);\
	_mm_storeu_si128((__m128i *)(keys[0].KEY+i*16), keyA);\
	_mm_storeu_si128((__m128i *)(keys[1].KEY+i*16), keyB);	\
	}

#define KS_round_4(i) { x2 =_mm_shuffle_epi8(keyA, mask); \
	keyA_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(0, keyA, keyA_aux);\
	x2 =_mm_shuffle_epi8(keyB, mask); \
	keyB_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(1, keyB, keyB_aux);\
	x2 =_mm_shuffle_epi8(keyC, mask); \
	keyC_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(2, keyC, keyC_aux);\
	x2 =_mm_shuffle_epi8(keyD, mask); \
	keyD_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(3, keyD, keyD_aux);\
	con=_mm_slli_epi32(con, 1);\
	_mm_storeu_si128((__m128i *)(keys[0].KEY+i*16), keyA);\
	_mm_storeu_si128((__m128i *)(keys[1].KEY+i*16), keyB);	\
	_mm_storeu_si128((__m128i *)(keys[2].KEY+i*16), keyC);	\
	_mm_storeu_si128((__m128i *)(keys[3].KEY+i*16), keyD);	\
	}

#define KS_round_4_last(i) { x2 =_mm_shuffle_epi8(keyA, mask); \
	keyA_aux=_mm_aesenclast_si128 (x2, con); \
	x2 =_mm_shuffle_epi8(keyB, mask); \
	keyB_aux=_mm_aesenclast_si128 (x2, con); \
	x2 =_mm_shuffle_epi8(keyC, mask); \
	keyC_aux=_mm_aesenclast_si128 (x2, con); \
	x2 =_mm_shuffle_epi8(keyD, mask); \
	keyD_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(0, keyA, keyA_aux);\
	KS_BLOCK(1, keyB, keyB_aux);\
	KS_BLOCK(2, keyC, keyC_aux);\
	KS_BLOCK(3, keyD, keyD_aux);\
	_mm_storeu_si128((__m128i *)(keys[0].KEY+i*16), keyA);\
	_mm_storeu_si128((__m128i *)(keys[1].KEY+i*16), keyB);	\
	_mm_storeu_si128((__m128i *)(keys[2].KEY+i*16), keyC);	\
	_mm_storeu_si128((__m128i *)(keys[3].KEY+i*16), keyD);	\
	}

#define KS_round_8(i) { x2 =_mm_shuffle_epi8(keyA, mask); \
	keyA_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(0, keyA, keyA_aux);\
	x2 =_mm_shuffle_epi8(keyB, mask); \
	keyB_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(1, keyB, keyB_aux);\
	x2 =_mm_shuffle_epi8(keyC, mask); \
	keyC_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(2, keyC, keyC_aux);\
	x2 =_mm_shuffle_epi8(keyD, mask); \
	keyD_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(3, keyD, keyD_aux);\
	x2 =_mm_shuffle_epi8(keyE, mask); \
	keyE_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(4, keyE, keyE_aux);\
	x2 =_mm_shuffle_epi8(keyF, mask); \
	keyF_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(5, keyF, keyF_aux);\
	x2 =_mm_shuffle_epi8(keyG, mask); \
	keyG_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(6, keyG, keyG_aux);\
	x2 =_mm_shuffle_epi8(keyH, mask); \
	keyH_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(7, keyH, keyH_aux);\
	con=_mm_slli_epi32(con, 1);\
	_mm_storeu_si128((__m128i *)(keys[0].KEY+i*16), keyA);\
	_mm_storeu_si128((__m128i *)(keys[1].KEY+i*16), keyB);	\
	_mm_storeu_si128((__m128i *)(keys[2].KEY+i*16), keyC);	\
	_mm_storeu_si128((__m128i *)(keys[3].KEY+i*16), keyD);	\
	_mm_storeu_si128((__m128i *)(keys[4].KEY+i*16), keyE);\
	_mm_storeu_si128((__m128i *)(keys[5].KEY+i*16), keyF);	\
	_mm_storeu_si128((__m128i *)(keys[6].KEY+i*16), keyG);	\
	_mm_storeu_si128((__m128i *)(keys[7].KEY+i*16), keyH);	\
	}

#define KS_round_8_last(i) { x2 =_mm_shuffle_epi8(keyA, mask); \
	keyA_aux=_mm_aesenclast_si128 (x2, con); \
	x2 =_mm_shuffle_epi8(keyB, mask); \
	keyB_aux=_mm_aesenclast_si128 (x2, con); \
	x2 =_mm_shuffle_epi8(keyC, mask); \
	keyC_aux=_mm_aesenclast_si128 (x2, con); \
	x2 =_mm_shuffle_epi8(keyD, mask); \
	keyD_aux=_mm_aesenclast_si128 (x2, con); \
	x2 =_mm_shuffle_epi8(keyE, mask); \
	keyE_aux=_mm_aesenclast_si128 (x2, con); \
	x2 =_mm_shuffle_epi8(keyF, mask); \
	keyF_aux=_mm_aesenclast_si128 (x2, con); \
	x2 =_mm_shuffle_epi8(keyG, mask); \
	keyG_aux=_mm_aesenclast_si128 (x2, con); \
	x2 =_mm_shuffle_epi8(keyH, mask); \
	keyH_aux=_mm_aesenclast_si128 (x2, con); \
	KS_BLOCK(0, keyA, keyA_aux);\
	KS_BLOCK(1, keyB, keyB_aux);\
	KS_BLOCK(2, keyC, keyC_aux);\
	KS_BLOCK(3, keyD, keyD_aux);\
	KS_BLOCK(4, keyE, keyE_aux);\
	KS_BLOCK(5, keyF, keyF_aux);\
	KS_BLOCK(6, keyG, keyG_aux);\
	KS_BLOCK(7, keyH, keyH_aux);\
	_mm_storeu_si128((__m128i *)(keys[0].KEY+i*16), keyA);\
	_mm_storeu_si128((__m128i *)(keys[1].KEY+i*16), keyB);	\
	_mm_storeu_si128((__m128i *)(keys[2].KEY+i*16), keyC);	\
	_mm_storeu_si128((__m128i *)(keys[3].KEY+i*16), keyD);	\
	_mm_storeu_si128((__m128i *)(keys[4].KEY+i*16), keyE);\
	_mm_storeu_si128((__m128i *)(keys[5].KEY+i*16), keyF);	\
	_mm_storeu_si128((__m128i *)(keys[6].KEY+i*16), keyG);	\
	_mm_storeu_si128((__m128i *)(keys[7].KEY+i*16), keyH);	\
	}

#define READ_KEYS_2(i) {keyA = _mm_loadu_si128((__m128i const*)(keys[0].KEY+i*16));\
	keyB = _mm_loadu_si128((__m128i const*)(keys[1].KEY+i*16));\
	}

#define READ_KEYS_4(i) {keyA = _mm_loadu_si128((__m128i const*)(keys[0].KEY+i*16));\
	keyB = _mm_loadu_si128((__m128i const*)(keys[1].KEY+i*16));\
	keyC = _mm_loadu_si128((__m128i const*)(keys[2].KEY+i*16));\
	keyD = _mm_loadu_si128((__m128i const*)(keys[3].KEY+i*16));\
	}

#define READ_KEYS_8(i) {keyA = _mm_loadu_si128((__m128i const*)(keys[0].KEY+i*16));\
	keyB = _mm_loadu_si128((__m128i const*)(keys[1].KEY+i*16));\
	keyC = _mm_loadu_si128((__m128i const*)(keys[2].KEY+i*16));\
	keyD = _mm_loadu_si128((__m128i const*)(keys[3].KEY+i*16));\
	keyE = _mm_loadu_si128((__m128i const*)(keys[4].KEY+i*16));\
	keyF = _mm_loadu_si128((__m128i const*)(keys[5].KEY+i*16));\
	keyG = _mm_loadu_si128((__m128i const*)(keys[6].KEY+i*16));\
	keyH = _mm_loadu_si128((__m128i const*)(keys[7].KEY+i*16));\
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

#define ENC_round_48(i) {block1=_mm_aesenc_si128(block1, (*(__m128i const*)(keys[0].KEY+i*16))); \
	block2=_mm_aesenc_si128(block2, (*(__m128i const*)(keys[0].KEY+i*16))); \
	block3=_mm_aesenc_si128(block3, (*(__m128i const*)(keys[1].KEY+i*16))); \
	block4=_mm_aesenc_si128(block4, (*(__m128i const*)(keys[1].KEY+i*16))); \
	block5=_mm_aesenc_si128(block5, (*(__m128i const*)(keys[2].KEY+i*16))); \
	block6=_mm_aesenc_si128(block6, (*(__m128i const*)(keys[2].KEY+i*16))); \
	block7=_mm_aesenc_si128(block7, (*(__m128i const*)(keys[3].KEY+i*16))); \
	block8=_mm_aesenc_si128(block8, (*(__m128i const*)(keys[3].KEY+i*16))); \
	}	
	
#define ENC_round_48_last(i) {block1=_mm_aesenclast_si128(block1, (*(__m128i const*)(keys[0].KEY+i*16))); \
	block2=_mm_aesenclast_si128(block2, (*(__m128i const*)(keys[0].KEY+i*16))); \
	block3=_mm_aesenclast_si128(block3, (*(__m128i const*)(keys[1].KEY+i*16))); \
	block4=_mm_aesenclast_si128(block4, (*(__m128i const*)(keys[1].KEY+i*16))); \
	block5=_mm_aesenclast_si128(block5, (*(__m128i const*)(keys[2].KEY+i*16))); \
	block6=_mm_aesenclast_si128(block6, (*(__m128i const*)(keys[2].KEY+i*16))); \
	block7=_mm_aesenclast_si128(block7, (*(__m128i const*)(keys[3].KEY+i*16))); \
	block8=_mm_aesenclast_si128(block8, (*(__m128i const*)(keys[3].KEY+i*16))); \
	}

#define ENC_round_88(i) {block1=_mm_aesenc_si128(block1, (*(__m128i const*)(keys[0].KEY+i*16))); \
	block2=_mm_aesenc_si128(block2, (*(__m128i const*)(keys[1].KEY+i*16))); \
	block3=_mm_aesenc_si128(block3, (*(__m128i const*)(keys[2].KEY+i*16))); \
	block4=_mm_aesenc_si128(block4, (*(__m128i const*)(keys[3].KEY+i*16))); \
	block5=_mm_aesenc_si128(block5, (*(__m128i const*)(keys[4].KEY+i*16))); \
	block6=_mm_aesenc_si128(block6, (*(__m128i const*)(keys[5].KEY+i*16))); \
	block7=_mm_aesenc_si128(block7, (*(__m128i const*)(keys[6].KEY+i*16))); \
	block8=_mm_aesenc_si128(block8, (*(__m128i const*)(keys[7].KEY+i*16))); \
	}	
	
#define ENC_round_88_last(i) {block1=_mm_aesenclast_si128(block1, (*(__m128i const*)(keys[0].KEY+i*16))); \
	block2=_mm_aesenclast_si128(block2, (*(__m128i const*)(keys[1].KEY+i*16))); \
	block3=_mm_aesenclast_si128(block3, (*(__m128i const*)(keys[2].KEY+i*16))); \
	block4=_mm_aesenclast_si128(block4, (*(__m128i const*)(keys[3].KEY+i*16))); \
	block5=_mm_aesenclast_si128(block5, (*(__m128i const*)(keys[4].KEY+i*16))); \
	block6=_mm_aesenclast_si128(block6, (*(__m128i const*)(keys[5].KEY+i*16))); \
	block7=_mm_aesenclast_si128(block7, (*(__m128i const*)(keys[6].KEY+i*16))); \
	block8=_mm_aesenclast_si128(block8, (*(__m128i const*)(keys[7].KEY+i*16))); \
	}

static block sigma(block a) {
	return xorBlocks(_mm_shuffle_epi32(a, 78), _mm_and_si128(a, _mm_set_epi64x(0xFFFFFFFFFFFFFFFF, 0x00)));
}

/*
 * AES key scheduling for 2/4/8 keys
 */
static inline void AES_ks2(block* user_key, ROUND_KEYS *KEYS) {
	unsigned char *first_key = (unsigned char*)user_key;
	ROUND_KEYS *keys = KEYS;
	__m128i keyA, keyB, con, mask, x2, keyA_aux, keyB_aux, globAux;
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
	
	KS_round_2(1)
	KS_round_2(2)
	KS_round_2(3)
	KS_round_2(4)
	KS_round_2(5)
	KS_round_2(6)
	KS_round_2(7)
	KS_round_2(8)

	con = _mm_loadu_si128((__m128i const*)_con2);			

	KS_round_2(9)
	KS_round_2_last(10)
}

static inline void AES_ks4(block* user_key, ROUND_KEYS *KEYS) {
	unsigned char *first_key = (unsigned char*)user_key;
	ROUND_KEYS *keys = KEYS;
	__m128i keyA, keyB, keyC, keyD, con, mask, x2, keyA_aux, keyB_aux, keyC_aux, keyD_aux, globAux;
	int _con1[4]={1,1,1,1};
	int _con2[4]={0x1b,0x1b,0x1b,0x1b};
	int _mask[4]={0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d};
	unsigned int _con3[4]={0x0ffffffff, 0x0ffffffff, 0x07060504, 0x07060504};
	__m128i con3=_mm_loadu_si128((__m128i const*)_con3);
	
	keys[0].nr=10;
	keys[1].nr=10;
	keys[2].nr=10;
	keys[3].nr=10;

	keyA = _mm_loadu_si128((__m128i const*)(first_key));	
	keyB = _mm_loadu_si128((__m128i const*)(first_key+16));	
	keyC = _mm_loadu_si128((__m128i const*)(first_key+32));	
	keyD = _mm_loadu_si128((__m128i const*)(first_key+48));	
	
	_mm_storeu_si128((__m128i *)keys[0].KEY, keyA);
	_mm_storeu_si128((__m128i *)keys[1].KEY, keyB);
	_mm_storeu_si128((__m128i *)keys[2].KEY, keyC);
	_mm_storeu_si128((__m128i *)keys[3].KEY, keyD);


	con = _mm_loadu_si128((__m128i const*)_con1);	
	mask = _mm_loadu_si128((__m128i const*)_mask);	
	
	KS_round_4(1)
	KS_round_4(2)
	KS_round_4(3)
	KS_round_4(4)
	KS_round_4(5)
	KS_round_4(6)
	KS_round_4(7)
	KS_round_4(8)

	con = _mm_loadu_si128((__m128i const*)_con2);			

	KS_round_4(9)
	KS_round_4_last(10)
}

static inline void AES_ks8(block* user_key, ROUND_KEYS *KEYS) {
	unsigned char *first_key = (unsigned char*)user_key;
	ROUND_KEYS *keys = KEYS;
	__m128i keyA, keyB, keyC, keyD, keyE, keyF, keyG, keyH, keyA_aux, keyB_aux, keyC_aux, keyD_aux, keyE_aux, keyF_aux, keyG_aux, keyH_aux;
	__m128i con, mask, x2, globAux;
	int _con1[4]={1,1,1,1};
	int _con2[4]={0x1b,0x1b,0x1b,0x1b};
	int _mask[4]={0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d};
	unsigned int _con3[4]={0x0ffffffff, 0x0ffffffff, 0x07060504, 0x07060504};
	__m128i con3 = _mm_loadu_si128((__m128i const*)_con3);

	keys[0].nr=10;
	keys[1].nr=10;
	keys[2].nr=10;
	keys[3].nr=10;
	keys[4].nr=10;
	keys[5].nr=10;
	keys[6].nr=10;
	keys[7].nr=10;

	keyA = _mm_loadu_si128((__m128i const*)(first_key));	
	keyB = _mm_loadu_si128((__m128i const*)(first_key+16));	
	keyC = _mm_loadu_si128((__m128i const*)(first_key+32));	
	keyD = _mm_loadu_si128((__m128i const*)(first_key+48));	
	keyE = _mm_loadu_si128((__m128i const*)(first_key+64));	
	keyF = _mm_loadu_si128((__m128i const*)(first_key+80));	
	keyG = _mm_loadu_si128((__m128i const*)(first_key+96));	
	keyH = _mm_loadu_si128((__m128i const*)(first_key+112));	

	_mm_storeu_si128((__m128i *)keys[0].KEY, keyA);	
	_mm_storeu_si128((__m128i *)keys[1].KEY, keyB);	
	_mm_storeu_si128((__m128i *)keys[2].KEY, keyC);
	_mm_storeu_si128((__m128i *)keys[3].KEY, keyD);
	_mm_storeu_si128((__m128i *)keys[4].KEY, keyE);	
	_mm_storeu_si128((__m128i *)keys[5].KEY, keyF);	
	_mm_storeu_si128((__m128i *)keys[6].KEY, keyG);
	_mm_storeu_si128((__m128i *)keys[7].KEY, keyH);

	con = _mm_loadu_si128((__m128i const*)_con1);	
	mask = _mm_loadu_si128((__m128i const*)_mask);	
	
	KS_round_8(1)
	KS_round_8(2)
	KS_round_8(3)
	KS_round_8(4)
	KS_round_8(5)
	KS_round_8(6)
	KS_round_8(7)
	KS_round_8(8)

	con = _mm_loadu_si128((__m128i const*)_con2);			

	KS_round_8(9)
	KS_round_8_last(10)
}

/*
 * AES key scheduling for circuit generation with 2/4/8 keys
 */
static inline void AES_ks2_index(block random, uint64_t idx, ROUND_KEYS *KEYS) {
	block user_key[2];
	user_key[0] = xorBlocks(makeBlock(2 * idx, (uint64_t)0), random);
	user_key[1] = xorBlocks(makeBlock(2 * idx + 1, (uint64_t)0), random);

	AES_ks2(user_key, KEYS);
}

static inline void AES_ks4_index(block random, uint64_t idx, ROUND_KEYS *KEYS) {
	block user_key[4];
	user_key[0] = xorBlocks(makeBlock(2 * idx, (uint64_t)0), random);
	user_key[1] = xorBlocks(makeBlock(2 * idx + 1, (uint64_t)0), random);
	idx++;
	user_key[2] = xorBlocks(makeBlock(2 * idx, (uint64_t)0), random);
	user_key[3] = xorBlocks(makeBlock(2 * idx + 1, (uint64_t)0), random);
	
	AES_ks4(user_key, KEYS);
}

static inline void AES_ks8_index(block random, uint64_t idx, ROUND_KEYS *KEYS) {
	block user_key[8];
	user_key[0] = xorBlocks(makeBlock(2 * idx, (uint64_t)0), random);
	user_key[1] = xorBlocks(makeBlock(2 * idx + 1, (uint64_t)0), random);
	idx++;
	user_key[2] = xorBlocks(makeBlock(2 * idx, (uint64_t)0), random);
	user_key[3] = xorBlocks(makeBlock(2 * idx + 1, (uint64_t)0), random);
	idx++;
	user_key[4] = xorBlocks(makeBlock(2 * idx, (uint64_t)0), random);
	user_key[5] = xorBlocks(makeBlock(2 * idx + 1, (uint64_t)0), random);
	idx++;
	user_key[6] = xorBlocks(makeBlock(2 * idx, (uint64_t)0), random);
	user_key[7] = xorBlocks(makeBlock(2 * idx + 1, (uint64_t)0), random);
	
	AES_ks8(user_key, KEYS);
}

/*
 * AES encryptin with 
 * 2 keys 2 ciphers
 * 2 keys 4 ciphers
 * 4 keys 8 ciphers
 * 8 keys 8 ciphers
 */
static inline void AES_ecb_ccr_ks2_enc2(block *plaintext, block *ciphertext, ROUND_KEYS *KEYS) {
	unsigned char* PT = (unsigned char*)plaintext;
	unsigned char* CT = (unsigned char*)ciphertext;
	ROUND_KEYS *keys = KEYS;
	__m128i keyA, keyB;
	__m128i block1 = _mm_loadu_si128((__m128i const*)(0*16+PT));	
	__m128i block2 = _mm_loadu_si128((__m128i const*)(1*16+PT));	
	READ_KEYS_2(0)
	
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

}

static inline void AES_ecb_ccr_ks2_enc4(block *plaintext, block *ciphertext, ROUND_KEYS *KEYS) {
	unsigned char* PT = (unsigned char*)plaintext;
	unsigned char* CT = (unsigned char*)ciphertext;
	ROUND_KEYS *keys = KEYS;
	__m128i keyA, keyB;
	
	__m128i block1 = _mm_loadu_si128((__m128i const*)(0*16+PT));	
	__m128i block2 = _mm_loadu_si128((__m128i const*)(1*16+PT));	
	__m128i block3 = _mm_loadu_si128((__m128i const*)(2*16+PT));	
	__m128i block4 = _mm_loadu_si128((__m128i const*)(3*16+PT));	
		
	READ_KEYS_2(0)
	
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
}

static inline void AES_ecb_ccr_ks4_enc8(block *plaintext, block *ciphertext, ROUND_KEYS *KEYS) {
	unsigned char* PT = (unsigned char*)plaintext;
	unsigned char* CT = (unsigned char*)ciphertext;
	ROUND_KEYS *keys = KEYS;
	__m128i keyA, keyB, keyC, keyD;
	
	__m128i block1 = _mm_loadu_si128((__m128i const*)(0*16+PT));	
	__m128i block2 = _mm_loadu_si128((__m128i const*)(1*16+PT));	
	__m128i block3 = _mm_loadu_si128((__m128i const*)(2*16+PT));	
	__m128i block4 = _mm_loadu_si128((__m128i const*)(3*16+PT));		
	__m128i block5 = _mm_loadu_si128((__m128i const*)(4*16+PT));	
	__m128i block6 = _mm_loadu_si128((__m128i const*)(5*16+PT));	
	__m128i block7 = _mm_loadu_si128((__m128i const*)(6*16+PT));	
	__m128i block8 = _mm_loadu_si128((__m128i const*)(7*16+PT));	

	READ_KEYS_4(0)
	
	block1 = _mm_xor_si128(keyA, block1);
	block2 = _mm_xor_si128(keyA, block2);
	block3 = _mm_xor_si128(keyB, block3);
	block4 = _mm_xor_si128(keyB, block4);
	block5 = _mm_xor_si128(keyC, block5);
	block6 = _mm_xor_si128(keyC, block6);
	block7 = _mm_xor_si128(keyD, block7);
	block8 = _mm_xor_si128(keyD, block8);

	ENC_round_48(1)
	ENC_round_48(2)
	ENC_round_48(3)
	ENC_round_48(4)
	ENC_round_48(5)
	ENC_round_48(6)
	ENC_round_48(7)
	ENC_round_48(8)
	ENC_round_48(9)
	ENC_round_48_last(10)
	
	_mm_storeu_si128((__m128i *)(CT+0*16), block1);	
	_mm_storeu_si128((__m128i *)(CT+1*16), block2);	
	_mm_storeu_si128((__m128i *)(CT+2*16), block3);	
	_mm_storeu_si128((__m128i *)(CT+3*16), block4);	
	_mm_storeu_si128((__m128i *)(CT+4*16), block5);	
	_mm_storeu_si128((__m128i *)(CT+5*16), block6);	
	_mm_storeu_si128((__m128i *)(CT+6*16), block7);	
	_mm_storeu_si128((__m128i *)(CT+7*16), block8);
}

static inline void AES_ecb_ccr_ks8_enc8(block *plaintext, block *ciphertext, ROUND_KEYS *KEYS) {
	unsigned char* PT = (unsigned char*)plaintext;
	unsigned char* CT = (unsigned char*)ciphertext;
	ROUND_KEYS *keys = KEYS;
	__m128i keyA, keyB, keyC, keyD, keyE, keyF, keyG, keyH;
	
	__m128i block1 = _mm_loadu_si128((__m128i const*)(0*16+PT));	
	__m128i block2 = _mm_loadu_si128((__m128i const*)(1*16+PT));	
	__m128i block3 = _mm_loadu_si128((__m128i const*)(2*16+PT));	
	__m128i block4 = _mm_loadu_si128((__m128i const*)(3*16+PT));		
	__m128i block5 = _mm_loadu_si128((__m128i const*)(4*16+PT));	
	__m128i block6 = _mm_loadu_si128((__m128i const*)(5*16+PT));	
	__m128i block7 = _mm_loadu_si128((__m128i const*)(6*16+PT));	
	__m128i block8 = _mm_loadu_si128((__m128i const*)(7*16+PT));	

	READ_KEYS_8(0)
	
	block1 = _mm_xor_si128(keyA, block1);
	block2 = _mm_xor_si128(keyB, block2);
	block3 = _mm_xor_si128(keyC, block3);
	block4 = _mm_xor_si128(keyD, block4);
	block5 = _mm_xor_si128(keyE, block5);
	block6 = _mm_xor_si128(keyF, block6);
	block7 = _mm_xor_si128(keyG, block7);
	block8 = _mm_xor_si128(keyH, block8);

	ENC_round_88(1)
	ENC_round_88(2)
	ENC_round_88(3)
	ENC_round_88(4)
	ENC_round_88(5)
	ENC_round_88(6)
	ENC_round_88(7)
	ENC_round_88(8)
	ENC_round_88(9)
	ENC_round_88_last(10)
	
	_mm_storeu_si128((__m128i *)(CT+0*16), block1);	
	_mm_storeu_si128((__m128i *)(CT+1*16), block2);	
	_mm_storeu_si128((__m128i *)(CT+2*16), block3);	
	_mm_storeu_si128((__m128i *)(CT+3*16), block4);	
	_mm_storeu_si128((__m128i *)(CT+4*16), block5);	
	_mm_storeu_si128((__m128i *)(CT+5*16), block6);	
	_mm_storeu_si128((__m128i *)(CT+6*16), block7);	
	_mm_storeu_si128((__m128i *)(CT+7*16), block8);
}

}
#endif
