#ifndef UTIL_BLOCK_H__
#define UTIL_BLOCK_H__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <xmmintrin.h>
#include <emmintrin.h>
#include <smmintrin.h>
#include <wmmintrin.h>
#include <assert.h>

namespace emp {
typedef __m128i block;

inline bool getLSB(const block & x) {
	return (*((char*)&x)&1) == 1;
}
__attribute__((target("sse2")))
inline block makeBlock(int64_t x, int64_t y) {
	return _mm_set_epi64x(x, y);
}
__attribute__((target("sse2")))
inline block zero_block() {
	return _mm_setzero_si128();
}
inline block one_block() {
	return makeBlock(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL);
}

__attribute__((target("sse2")))
inline block make_delta(const block & a) {
	return _mm_or_si128(makeBlock(0L, 1L), a);
}

typedef __m128i block_tpl[2];

__attribute__((target("sse2")))
inline block xorBlocks(block x, block y) {
	return _mm_xor_si128(x,y);
}
__attribute__((target("sse2")))
inline block andBlocks(block x, block y) {
	return _mm_and_si128(x,y);
}

inline void xorBlocks_arr(block* res, const block* x, const block* y, int nblocks) {
	const block * dest = nblocks+x;
	for (; x != dest;) {
		*(res++) = xorBlocks(*(x++), *(y++));
	}
}
inline void xorBlocks_arr(block* res, const block* x, block y, int nblocks) {
	const block * dest = nblocks+x;
	for (; x != dest;) {
		*(res++) = xorBlocks(*(x++), y);
	}
}

__attribute__((target("sse4")))
inline bool cmpBlock(const block * x, const block * y, int nblocks) {
	const block * dest = nblocks+x;
	for (; x != dest;) {
		__m128i vcmp = _mm_xor_si128(*(x++), *(y++));
		if(!_mm_testz_si128(vcmp, vcmp))
			return false;
	}
	return true;
}

//deprecate soon
inline bool block_cmp(const block * x, const block * y, int nblocks) {
	return cmpBlock(x,y,nblocks);
}

__attribute__((target("sse4")))
inline bool isZero(const block * b) {
	return _mm_testz_si128(*b,*b) > 0;
}

__attribute__((target("sse4")))
inline bool isOne(const block * b) {
	__m128i neq = _mm_xor_si128(*b, one_block());
	return _mm_testz_si128(neq, neq) > 0;
}

__attribute__((target("sse4")))
inline block sigma(block a) {
	return xorBlocks(_mm_shuffle_epi32(a, 78), _mm_and_si128(a, _mm_set_epi64x(0xFFFFFFFFFFFFFFFF, 0x00)));
}

	
//Modified from
//https://mischasan.wordpress.com/2011/10/03/the-full-sse2-bit-matrix-transpose-routine/
// with inner most loops changed to _mm_set_epi8 and _mm_set_epi16
#define INP(x, y) inp[(x)*ncols / 8 + (y) / 8]
#define OUT(x, y) out[(y)*nrows / 8 + (x) / 8]

__attribute__((target("sse2")))
inline void sse_trans(uint8_t *out, uint8_t const *inp, uint64_t nrows,
               uint64_t ncols) {
  uint64_t rr, cc;
  int i, h;
  union {
    __m128i x;
    uint8_t b[16];
  } tmp;
  __m128i vec;
  assert(nrows % 8 == 0 && ncols % 8 == 0);

  // Do the main body in 16x8 blocks:
  for (rr = 0; rr <= nrows - 16; rr += 16) {
    for (cc = 0; cc < ncols; cc += 8) {
      vec = _mm_set_epi8(INP(rr + 15, cc), INP(rr + 14, cc), INP(rr + 13, cc),
                         INP(rr + 12, cc), INP(rr + 11, cc), INP(rr + 10, cc),
                         INP(rr + 9, cc), INP(rr + 8, cc), INP(rr + 7, cc),
                         INP(rr + 6, cc), INP(rr + 5, cc), INP(rr + 4, cc),
                         INP(rr + 3, cc), INP(rr + 2, cc), INP(rr + 1, cc),
                         INP(rr + 0, cc));
      for (i = 8; --i >= 0; vec = _mm_slli_epi64(vec, 1))
        *(uint16_t *)&OUT(rr, cc + i) = _mm_movemask_epi8(vec);
    }
  }
  if (rr == nrows)
    return;

  // The remainder is a block of 8x(16n+8) bits (n may be 0).
  //  Do a PAIR of 8x8 blocks in each step:
  if ((ncols % 8 == 0 && ncols % 16 != 0) ||
      (nrows % 8 == 0 && nrows % 16 != 0)) {
    // The fancy optimizations in the else-branch don't work if the above if-condition
    // holds, so we use the simpler non-simd variant for that case.
    for (cc = 0; cc <= ncols - 16; cc += 16) {
      for (i = 0; i < 8; ++i) {
        tmp.b[i] = h = *(uint16_t const *)&INP(rr + i, cc);
        tmp.b[i + 8] = h >> 8;
      }
      for (i = 8; --i >= 0; tmp.x = _mm_slli_epi64(tmp.x, 1)) {
        OUT(rr, cc + i) = h = _mm_movemask_epi8(tmp.x);
        OUT(rr, cc + i + 8) = h >> 8;
      }
    }
  } else {
    for (cc = 0; cc <= ncols - 16; cc += 16) {
      vec = _mm_set_epi16(*(uint16_t const *)&INP(rr + 7, cc),
                          *(uint16_t const *)&INP(rr + 6, cc),
                          *(uint16_t const *)&INP(rr + 5, cc),
                          *(uint16_t const *)&INP(rr + 4, cc),
                          *(uint16_t const *)&INP(rr + 3, cc),
                          *(uint16_t const *)&INP(rr + 2, cc),
                          *(uint16_t const *)&INP(rr + 1, cc),
                          *(uint16_t const *)&INP(rr + 0, cc));
      for (i = 8; --i >= 0; vec = _mm_slli_epi64(vec, 1)) {
        OUT(rr, cc + i) = h = _mm_movemask_epi8(vec);
        OUT(rr, cc + i + 8) = h >> 8;
      }
    }
  }
  if (cc == ncols)
    return;

  //  Do the remaining 8x8 block:
  for (i = 0; i < 8; ++i)
    tmp.b[i] = INP(rr + i, cc);
  for (i = 8; --i >= 0; tmp.x = _mm_slli_epi64(tmp.x, 1))
    OUT(rr, cc + i) = _mm_movemask_epi8(tmp.x);
}

const char fix_key[] = "\x61\x7e\x8d\xa2\xa0\x51\x1e\x96"
"\x5e\x41\xc2\x9b\x15\x3f\xc7\x7a";

/**
	  University of Bristol : Open Access Software Licence

	  Copyright (c) 2016, The University of Bristol, a chartered corporation having Royal Charter number RC000648 and a charity (number X1121) and its place of administration being at Senate House, Tyndall Avenue, Bristol, BS8 1TH, United Kingdom.

	  All rights reserved

	  Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

	  1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

	  2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

	  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


	  Any use of the software for scientific publications or commercial purposes should be reported to the University of Bristol (OSI-notifications@bristol.ac.uk and quote reference 1914). This is for impact and usage monitoring purposes only.

	  Enquiries about further applications and development opportunities are welcome. Please contact nigel@cs.bris.ac.uk

	  Contact GitHub API Training Shop Blog About
	 */
__attribute__((target("sse2,pclmul")))
inline void mul128(__m128i a, __m128i b, __m128i *res1, __m128i *res2) {
	/*	block a0xora1 = xorBlocks(a, _mm_srli_si128(a, 8));
		block b0xorb1 = xorBlocks(b, _mm_srli_si128(b, 8));

		block a0b0 = _mm_clmulepi64_si128(a, b, 0x00);
		block a1b1 = _mm_clmulepi64_si128(a, b, 0x11);
		block ab = _mm_clmulepi64_si128(a0xora1, b0xorb1, 0x00);

		block tmp = xorBlocks(a0b0, a1b1);
		tmp = xorBlocks(tmp, ab);

		*res1 = xorBlocks(a1b1, _mm_srli_si128(tmp, 8));
		*res2 = xorBlocks(a0b0, _mm_slli_si128(tmp, 8));*/
	__m128i tmp3, tmp4, tmp5, tmp6;
	tmp3 = _mm_clmulepi64_si128(a, b, 0x00);
	tmp4 = _mm_clmulepi64_si128(a, b, 0x10);
	tmp5 = _mm_clmulepi64_si128(a, b, 0x01);
	tmp6 = _mm_clmulepi64_si128(a, b, 0x11);

	tmp4 = _mm_xor_si128(tmp4, tmp5);
	tmp5 = _mm_slli_si128(tmp4, 8);
	tmp4 = _mm_srli_si128(tmp4, 8);
	tmp3 = _mm_xor_si128(tmp3, tmp5);
	tmp6 = _mm_xor_si128(tmp6, tmp4);
	// initial mul now in tmp3, tmp6
	*res1 = tmp3;
	*res2 = tmp6;
}
}
#endif//UTIL_BLOCK_H__
