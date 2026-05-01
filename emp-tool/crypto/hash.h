#ifndef EMP_HASH_H__
#define EMP_HASH_H__

#include "emp-tool/core/block.h"
#include "emp-tool/group/group.h"
#include <openssl/evp.h>
#include <stdio.h>

namespace emp {
class Hash { public:
	EVP_MD_CTX *mdctx;
	static const int DIGEST_SIZE = 32;
	Hash() {
		if((mdctx = EVP_MD_CTX_create()) == NULL)
			error("Hash function setup error!");
		if(1 != EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL))
			error("Hash function setup error!");
	}
	~Hash() {
		EVP_MD_CTX_destroy(mdctx);
	}
	// Owns a raw EVP_MD_CTX*; copying would double-destroy on dtor.
	Hash(const Hash&) = delete;
	Hash& operator=(const Hash&) = delete;
	void put(const void * data, int nbyte) {
		EVP_DigestUpdate(mdctx, data, nbyte);
	}
	void put_block(const block* blk, int nblock=1){
		put(blk, sizeof(block)*nblock);
	}
	// reset_after = false snapshots the running hash without disturbing
	// it: copies the EVP context, finalizes the copy, and discards it. The
	// original mdctx is untouched, so subsequent put()s continue to extend
	// the same transcript. Used for streaming Fiat-Shamir.
	void digest(void * a, bool reset_after = true) {
		if (reset_after) {
			uint32_t len = 0;
			EVP_DigestFinal_ex(mdctx, (unsigned char *)a, &len);
			reset();
		} else {
			EVP_MD_CTX *snap = EVP_MD_CTX_create();
			if (snap == NULL || 1 != EVP_MD_CTX_copy_ex(snap, mdctx))
				error("Hash snapshot error!");
			uint32_t len = 0;
			EVP_DigestFinal_ex(snap, (unsigned char *)a, &len);
			EVP_MD_CTX_destroy(snap);
		}
	}
	void reset() {
		EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL);
	}
	static void hash_once(void * dgst, const void * data, int nbyte) {
		// Per-thread persistent EVP_MD_CTX. The dominant cost of a
		// transient Hash object on small inputs is EVP_MD_CTX_new/free
		// (~700–1000 cy each); reusing a context drops that to a single
		// EVP_DigestInit_ex (a memset-class reset). thread_local gives
		// the same semantics as a fresh ctor for thread safety.
		struct Holder {
			EVP_MD_CTX * ctx;
			Holder() : ctx(EVP_MD_CTX_new()) {
				if (!ctx) error("hash_once: EVP_MD_CTX_new failed");
			}
			~Holder() { if (ctx) EVP_MD_CTX_free(ctx); }
		};
		thread_local Holder h;
		EVP_DigestInit_ex(h.ctx, EVP_sha256(), nullptr);
		EVP_DigestUpdate(h.ctx, data, nbyte);
		uint32_t len = 0;
		EVP_DigestFinal_ex(h.ctx, (unsigned char *)dgst, &len);
	}
	#ifdef __x86_64__
	__attribute__((target("sse2")))
	#endif
	static block hash_for_block(const void * data, int nbyte) {
		alignas(block) char digest[DIGEST_SIZE];
		hash_once(digest, data, nbyte);
		return _mm_load_si128((__m128i*)&digest[0]);
	}

	static block KDF(Point &in, uint64_t id = 1) {
		size_t len = in.size();
		in.group()->resize_scratch(len+8);
		unsigned char * tmp = in.group()->scratch();
		in.to_bin(tmp, len);
		memcpy(tmp+len, &id, 8);
		block ret = hash_for_block(tmp, len+8);
		return ret;
	}
};
}
#endif// HASH_H__
