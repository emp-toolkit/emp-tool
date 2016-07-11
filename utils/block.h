/*
	This file is part of JustGarble.

	JustGarble is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	JustGarble is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with JustGarble.  If not, see <http://www.gnu.org/licenses/>.

 */


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

typedef __m128i block;
typedef __m128i block_tpl[2];
inline block xorBlocks(block x, block y){return _mm_xor_si128(x,y);}
inline block andBlocks(block x, block y){return _mm_and_si128(x,y);}
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
inline void xorBlocks_arr2(block* res, const block* x, const block* y, int nblocks) {
	const block * dest = nblocks+x;
	for (; x != dest;) {
		*(res++) = xorBlocks(*(x++), *(y++));	
	}
}

inline bool block_cmp(const block * x, const block * y, int nblocks) {
	const block * dest = nblocks+x;
	for (; x != dest;) {
		__m128i vcmp = _mm_xor_si128(*(x++), *(y++)); 
		if(!_mm_testz_si128(vcmp, vcmp))
			return false;
	}
	return true;
}

#define zero_block() _mm_setzero_si128()
#define one_block() makeBlock(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL)
#define getLSB(x) (*((unsigned short *)&x)&1)
#define makeBlock(X,Y) _mm_set_epi64((__m64)(X), (__m64)(Y))

inline bool isZero(const block * b) {
	return _mm_testz_si128(*b,*b);
}

inline bool isOne(const block * b) {
	__m128i neq = _mm_xor_si128(*b, one_block());
	return _mm_testz_si128(neq, neq);
}


//Modified from
//https://mischasan.wordpress.com/2011/10/03/the-full-sse2-bit-matrix-transpose-routine/
// with inner most loops changed to _mm_set_epi8 and _mm_set_epi16
	inline void
sse_trans(uint8_t *out, uint8_t const *inp, int nrows, int ncols)
{
#   define INP(x,y) inp[(x)*ncols/8 + (y)/8]
#   define OUT(x,y) out[(y)*nrows/8 + (x)/8]
	int rr, cc, i, h;
	union { __m128i x; uint8_t b[16]; } tmp;
	__m128i vec;
	assert(nrows % 8 == 0 && ncols % 8 == 0);

	// Do the main body in 16x8 blocks:
	for (rr = 0; rr <= nrows - 16; rr += 16) {
		for (cc = 0; cc < ncols; cc += 8) {
			vec = _mm_set_epi8(
					INP(rr+15,cc),INP(rr+14,cc),INP(rr+13,cc),INP(rr+12,cc),INP(rr+11,cc),INP(rr+10,cc),INP(rr+9,cc),
					INP(rr+8,cc),INP(rr+7,cc),INP(rr+6,cc),INP(rr+5,cc),INP(rr+4,cc),INP(rr+3,cc),INP(rr+2,cc),INP(rr+1,cc),
					INP(rr+0,cc));
			for (i = 8; --i >= 0; vec = _mm_slli_epi64(vec, 1))
				*(uint16_t*)&OUT(rr,cc+i)= _mm_movemask_epi8(vec);
		}
	}
	if (rr == nrows) return;

	// The remainder is a block of 8x(16n+8) bits (n may be 0).
	//  Do a PAIR of 8x8 blocks in each step:
	for (cc = 0; cc <= ncols - 16; cc += 16) {
		vec = _mm_set_epi16(
				*(uint16_t const*)&INP(rr + 7, cc), *(uint16_t const*)&INP(rr + 6, cc),
				*(uint16_t const*)&INP(rr + 5, cc), *(uint16_t const*)&INP(rr + 4, cc),
				*(uint16_t const*)&INP(rr + 3, cc), *(uint16_t const*)&INP(rr + 2, cc),
				*(uint16_t const*)&INP(rr + 1, cc), *(uint16_t const*)&INP(rr + 0, cc));
		for (i = 8; --i >= 0; vec = _mm_slli_epi64(vec, 1)) {
			OUT(rr, cc + i) = h = _mm_movemask_epi8(vec);
			OUT(rr, cc + i + 8) = h >> 8;
		}
	}
	if (cc == ncols) return;

	//  Do the remaining 8x8 block:
	for (i = 0; i < 8; ++i)
		tmp.b[i] = INP(rr + i, cc);
	for (i = 8; --i >= 0; tmp.x = _mm_slli_epi64(tmp.x, 1))
		OUT(rr, cc + i) = _mm_movemask_epi8(tmp.x);
}


const char fix_key[] = "\x61\x7e\x8d\xa2\xa0\x51\x1e\x96"
"\x5e\x41\xc2\x9b\x15\x3f\xc7\x7a";

/*------------------------------------------------------------------------
  / OCB Version 3 Reference Code (Optimized C)     Last modified 08-SEP-2012
  /-------------------------------------------------------------------------
  / Copyright (c) 2012 Ted Krovetz.
  /
  / Permission to use, copy, modify, and/or distribute this software for any
  / purpose with or without fee is hereby granted, provided that the above
  / copyright notice and this permission notice appear in all copies.
  /
  / THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  / WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  / MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  / ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  / WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  / ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  / OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
  /
  / Phillip Rogaway holds patents relevant to OCB. See the following for
  / his patent grant: http://www.cs.ucdavis.edu/~rogaway/ocb/grant.htm
  /
  / Special thanks to Keegan McAllister for suggesting several good improvements
  /
  / Comments are welcome: Ted Krovetz <ted@krovetz.net> - Dedicated to Laurel K
  /------------------------------------------------------------------------- */
static inline block double_block(block bl) {
	const __m128i mask = _mm_set_epi32(135,1,1,1);
	__m128i tmp = _mm_srai_epi32(bl, 31);
	tmp = _mm_and_si128(tmp, mask);
	tmp = _mm_shuffle_epi32(tmp, _MM_SHUFFLE(2,1,0,3));
	bl = _mm_slli_epi32(bl, 1);
	return _mm_xor_si128(bl,tmp);
}

static inline block LEFTSHIFT1(block bl) {
	const __m128i mask = _mm_set_epi32(0,0, (1<<31),0);
	__m128i tmp = _mm_and_si128(bl,mask);
	bl = _mm_slli_epi64(bl, 1);
	return _mm_xor_si128(bl,tmp);
}
static inline block RIGHTSHIFT(block bl) {
	const __m128i mask = _mm_set_epi32(0,1,0,0);
	__m128i tmp = _mm_and_si128(bl,mask);
	bl = _mm_slli_epi64(bl, 1);
	return _mm_xor_si128(bl,tmp);
}
#endif//UTIL_BLOCK_H__
