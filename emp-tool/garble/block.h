#ifndef LIBGARBLE_BLOCK_H
#define LIBGARBLE_BLOCK_H

#include <wmmintrin.h>
#include <emmintrin.h>
#include <xmmintrin.h>
#include <smmintrin.h>

//typedef __m128i block;
#include <cryptoTools/Common/Defines.h>

using namespace osuCrypto;
#define garble_xor(x,y) _mm_xor_si128(x,y)
#define garble_zero_block() _mm_setzero_si128()
#define garble_equal(x,y) (_mm_movemask_epi8(_mm_cmpeq_epi8(x,y)) == 0xffff)
#define garble_unequal(x,y) (_mm_movemask_epi8(_mm_cmpeq_epi8(x,y)) != 0xffff)

#define garble_lsb(x) (*((char *) &x) & 1)
#define garble_make_block(X,Y) _mm_set_epi64x((long long)(X), (long long)(Y))
#define garble_double(B) _mm_slli_epi64(B,1)

#include <stdio.h>

block
garble_seed(block *seed);
block
garble_random_block(void);
block *
garble_allocate_blocks(size_t nblocks);

int
block_vfprintf(FILE *stream, const char *format, va_list ap);
int
block_fprintf(FILE *stream, const char *format, ...);
int
block_printf(const char *format, ...);

#endif
