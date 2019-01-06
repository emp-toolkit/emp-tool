#ifndef PRG_H__
#define PRG_H__
#include "emp-tool/utils/block.h"
#include "emp-tool/garble/aes.h"
#include "emp-tool/utils/constants.h"
#include "emp-tool/utils/utils_ec.h"
#include <gmp.h>
#include <random>

#ifdef EMP_USE_RANDOM_DEVICE
#else
#include <x86intrin.h>
#endif


/** @addtogroup BP
  @{
 */
namespace emp {

class PRG { public:
	uint64_t counter = 0;
	AES_KEY aes;
	PRG(const void * seed = nullptr, int id = 0) {	
		if (seed != nullptr) {
			reseed(seed, id);
		} else {
			block v;
#ifdef EMP_USE_RANDOM_DEVICE
			int * data = (int *)(&v);
			std::random_device rand_div;
			for (size_t i = 0; i < sizeof(block) / sizeof(int); ++i)
				data[i] = rand_div();
#else
			unsigned long long r0, r1;
			_rdseed64_step(&r0);
			_rdseed64_step(&r1);
			v = makeBlock(r0, r1);
#endif
			reseed(&v);
		}
	}
	void reseed(const void * key, uint64_t id = 0) {
		block v = _mm_loadu_si128((block*)key);
		v = xorBlocks(v, makeBlock(0LL, id));
		AES_set_encrypt_key(v, &aes);
		counter = 0;
	}

	void random_data(void *data, int nbytes) {
		random_block((block *)data, nbytes/16);
		if (nbytes % 16 != 0) {
			block extra;
			random_block(&extra, 1);
			memcpy((nbytes/16*16)+(char *) data, &extra, nbytes%16);
		}
	}

	void random_bool(bool * data, int length) {
		uint8_t * uint_data = (uint8_t*)data;
		random_data(uint_data, length);
		for(int i = 0; i < length; ++i)
			data[i] = uint_data[i] & 1;
	}

	void random_data_unaligned(void *data, int nbytes) {
		block tmp[AES_BATCH_SIZE];
		for(int i = 0; i < nbytes/(AES_BATCH_SIZE*16); i++) {
			random_block(tmp, AES_BATCH_SIZE);
			memcpy((16*i*AES_BATCH_SIZE)+(uint8_t*)data, tmp, 16*AES_BATCH_SIZE);
		}
		if (nbytes % (16*AES_BATCH_SIZE) != 0) {
			random_block(tmp, AES_BATCH_SIZE);
			memcpy((nbytes/(16*AES_BATCH_SIZE)*(16*AES_BATCH_SIZE))+(uint8_t*) data, tmp, nbytes%(16*AES_BATCH_SIZE));
		}
	}

	void random_block(block * data, int nblocks=1) {
		for (int i = 0; i < nblocks; ++i) {
			data[i] = makeBlock(0LL, counter++);
		}
		int i = 0;
		for(; i < nblocks-AES_BATCH_SIZE; i+=AES_BATCH_SIZE) {
			AES_ecb_encrypt_blks(data+i, AES_BATCH_SIZE, &aes);
		}
		AES_ecb_encrypt_blks(data+i, (AES_BATCH_SIZE >  nblocks-i) ? nblocks-i:AES_BATCH_SIZE, &aes);
	}

	template<typename T, typename ... L>
		void random_bn(T t, L... l) {
			random_bn(l...);
			random_bn(t);
		}

	void random_bn(bn_t a, int sign = BN_POS, int bits = BIT_LEN) {
		int digits;
		SPLIT(bits, digits, bits, DIG_LOG);
		digits += (bits > 0 ? 1 : 0);
		bn_grow(a, digits);
		random_data((uint8_t*)a->dp, digits * sizeof(dig_t));
		a->used = digits;
		a->sign = sign;
		if (bits > 0) {
			dig_t mask = ((dig_t)1 << (dig_t)bits) - 1;
			a->dp[a->used - 1] &= mask;
		}
		bn_trim(a);
	}

	void random_bn(bn_t *a, int length=1, int sign = BN_POS, int bits = BIT_LEN) {
		for(int i = 0; i < length; ++i)
			random_bn(a[i]);
	}

	template<typename T, typename ... L>
		void random_eb(T t, L... l) {
			random_eb(l...);
			random_eb(t);
		}

	void random_eb(eb_t p) {
		bn_t n, k;
		bn_new(k);
		bn_new(n);
		eb_curve_get_ord(n);
		random_bn(k, BN_POS, bn_bits(n));
		bn_mod(k, k, n);
		eb_mul_gen(p, k);
	}

	void random_eb(eb_t *p, int length=1) {
		bn_t n, k;
		bn_new(k);
		bn_new(n);
		eb_curve_get_ord(n);
		for(int i = 0; i < length; ++i) {
			random_bn(k, BN_POS, bn_bits(n));
			bn_mod(k, k, n);
			eb_mul_gen(p[i], k);
		}
	}

	void random_mpz(mpz_t out, int nbits) {
		int nbytes = (nbits+7)/8;
		uint8_t * data = (uint8_t *)new block[(nbytes+15)/16];
		random_data(data, nbytes);
		if (nbits % 8 != 0)
			data[0] %= (1 << (nbits % 8));
		mpz_import(out, nbytes, 1, 1, 0, 0, data);
		delete [] data;
	}

	void random_mpz(mpz_t rop, const mpz_t n) {
		auto size = mpz_sizeinbase(n, 2);
		while (1) {
			random_mpz(rop, (int)size);
			if (mpz_cmp(rop, n) < 0) {
				break;
			}
		}
	}
};
}
/**@}*/
#endif// PRP_H__
