#ifndef HASH_H__
#define HASH_H__

#include "emp-tool/utils/block.h"
#include "emp-tool/utils/constants.h"
#include <openssl/sha.h>
#include <stdio.h>

extern "C" {
#include <relic/relic.h>
}

/** @addtogroup BP
  @{
 */
namespace emp {
class Hash { public:
	SHA256_CTX hash;
	char buffer[HASH_BUFFER_SIZE];
	int size = 0;
	static const int DIGEST_SIZE = 32;
	Hash() {
		SHA256_Init(&hash);
	}
	~Hash() {
	}
	void put(const void * data, int nbyte) {
		if (nbyte > HASH_BUFFER_SIZE)
			SHA256_Update(&hash, data, nbyte);
		else if(size + nbyte < HASH_BUFFER_SIZE) {
			memcpy(buffer+size, data, nbyte);
			size+=nbyte;
		} else {
			SHA256_Update(&hash, (char*)buffer, size);
			memcpy(buffer, data, nbyte);
			size = nbyte;
		}
	}
	void put_block(const block* block, int nblock=1){
		put(block, sizeof(block)*nblock);
	}
	void digest(char * a) {
		if(size > 0) {
			SHA256_Update(&hash, (char*)buffer, size);
			size=0;
		}
		SHA256_Final((unsigned char *)a, &hash);
	}
	void reset() {
		SHA256_Init(&hash);
		size=0;
	}
	static void hash_once(void * digest, const void * data, int nbyte) {
		(void )SHA256((const unsigned char *)data, nbyte, (unsigned char *)digest);
	}
	static block hash_for_block(const void * data, int nbyte) {
		char digest[DIGEST_SIZE];
		hash_once(digest, data, nbyte);
		return _mm_load_si128((__m128i*)&digest[0]);
	}
	void put_eb(const eb_t * eb, int length) {
		uint8_t buffer[100];//large enough to hold one.
		for(int i = 0; i < length; ++i) {
			int eb_size = eb_size_bin(eb[i], false);
			eb_write_bin(buffer, eb_size, eb[i], false);
			put(buffer, eb_size);
		}
	}
};
}
/**@}*/
#endif// HASH_H__
