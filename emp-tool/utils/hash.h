#ifndef HASH_H__
#define HASH_H__
#include "emp-tool/utils/block.h"
#include "emp-tool/utils/config.h"
//#include <openssl/sha.h>
#include <cryptoTools/Crypto/sha1.h>
#include <stdio.h>
#include "utils_ec.h"
/** @addtogroup BP
    @{
  */

class Hash {
	osuCrypto::SHA1 hash;
	char buffer[HASH_BUFFER_SIZE];
	int size = 0;
	public:
	static const int DIGEST_SIZE = 20;
	Hash() {
		//SHA1_Init(&hash);
	}
	~Hash() {
	}
	void put(const void * data, int nbyte) {
		if (nbyte > HASH_BUFFER_SIZE)
		{
			hash.Update((uint8_t*) data, nbyte);
			//SHA1_Update(&hash, data, nbyte);
		}
		else if(size + nbyte < HASH_BUFFER_SIZE) {
			memcpy(buffer+size, data, nbyte);
			size+=nbyte;
		} else {
			hash.Update((uint8_t*)buffer, nbyte);

			//SHA1_Update(&hash, (char*)buffer, size);
			memcpy(buffer, data, nbyte);
			size = nbyte;
		}
	}
	void put_block(const block* block, int nblock=1){
		put(block, sizeof(block)*nblock);
	}
	void digest(char * a) {
		if(size > 0) {
			hash.Update((uint8_t*)buffer, size);
			//SHA1_Update(&hash, (char*)buffer, size);
			size=0;
		}
		//SHA1_Final((unsigned char *)a, &hash);
		hash.Final((uint8_t*)a);
	}
	void reset() {
		//SHA1_Init(&hash);
		hash.Reset();
		size=0;
	}
	static void hash_once(void * digest, const void * data, int nbyte) {
		using namespace osuCrypto;
		SHA1 sha;
		sha.Update((u8*)data, nbyte);
		sha.Final((u8*)digest);
		//(void )SHA1((const unsigned char *)data, nbyte, (unsigned char *)digest);
	}
	static block hash_for_block(const void * data, int nbyte) {
		char digest[20];
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
/**@}*/
#endif// HASH_H__
