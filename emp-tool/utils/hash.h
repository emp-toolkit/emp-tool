#ifndef HASH_H__
#define HASH_H__

#include "emp-tool/utils/block.h"
#include "emp-tool/utils/constants.h"
#include <openssl/sha.h>
#include <stdio.h>

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
	__attribute__((target("sse2")))
	static block hash_for_block(const void * data, int nbyte) {
		char digest[DIGEST_SIZE];
		hash_once(digest, data, nbyte);
		return _mm_load_si128((__m128i*)&digest[0]);
	}

	static block KDF(Point &in, uint64_t id = 1) {
		size_t len = in.size();
		in.group->resize_scratch(len+8);
		unsigned char * tmp = in.group->scratch;
		in.to_bin(tmp, len);
		memcpy(tmp+len, &id, 8);
		block ret = hash_for_block(tmp, len+8);
		return ret;
	}
};
}
/**@}*/
#endif// HASH_H__
