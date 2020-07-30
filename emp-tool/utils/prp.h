#include "emp-tool/utils/block.h"
#include "emp-tool/utils/constants.h"
#include "emp-tool/garble/aes.h"
#include <stdio.h>
#ifndef PRP_H__
#define PRP_H__
/** @addtogroup BP
  @{
 */
namespace emp {

class PRP { public:
	AES_KEY aes;

	PRP(const char * seed = fix_key) {
		aes_set_key(seed);
	}

	PRP(const block& seed): PRP((const char *)&seed) {
	}

	void aes_set_key(const char * key) {
		aes_set_key(_mm_loadu_si128((block*)key));
	}

	void aes_set_key(const block& v) {
		AES_set_encrypt_key(v, &aes);
	}

	void permute_block(block *data, int nblocks) {
		int i = 0;
		for(; i < nblocks-AES_BATCH_SIZE; i+=AES_BATCH_SIZE) {
			AES_ecb_encrypt_blks(data+i, AES_BATCH_SIZE, &aes);
		}
		AES_ecb_encrypt_blks(data+i, (AES_BATCH_SIZE >  nblocks-i) ? nblocks-i:AES_BATCH_SIZE, &aes);
	}

	void permute_data(void*data, int nbytes) {
		permute_block((block *)data, nbytes/16);
		if (nbytes % 16 != 0) {
			uint8_t extra[16];
			memset(extra, 0, 16);
			memcpy(extra, (nbytes/16*16)+(char *) data, nbytes%16);
			permute_block((block*)extra, 1);
			memcpy((nbytes/16*16)+(char *) data, &extra, nbytes%16);
		}
	}

	block H(block in, uint64_t id) {
		in = sigma(in);
		__m128i k_128 = _mm_loadl_epi64( (__m128i const *) (&id));
		in = xorBlocks(in, k_128);
		block t = in;
		permute_block(&t, 1);
		in =  xorBlocks(in, t);
		return in;	
	}
	template<int n>
		void H(block out[n], block in[n], uint64_t id) {
			block scratch[n];
			for(int i = 0; i < n; ++i) {
				out[i] = scratch[i] = xorBlocks(sigma(in[i]), _mm_loadl_epi64( (__m128i const *) (&id)));
				++id;
			}
			permute_block(scratch, n);
			xorBlocks_arr(out, scratch, out, n);
		}

	void Hn(block*out, block* in, uint64_t id, int length, block * scratch = nullptr) {
		bool del = false;
		if(scratch == nullptr) {
			del = true;
			scratch = new block[length];
		}
		for(int i = 0; i < length; ++i){
			out[i] = scratch[i] = xorBlocks(sigma(in[i]), _mm_loadl_epi64( (__m128i const *) (&id)));
			++id;
		}
		permute_block(scratch, length);
		xorBlocks_arr(out, scratch, out, length);
		if(del) {
			delete[] scratch;
			scratch = nullptr;
		}
	}
};
}
/**@}*/
#endif// PRP_H__
