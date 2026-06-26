#ifndef EMP_HASH_H__
#define EMP_HASH_H__

#include "emp-tool/runtime/core/block.h"
#include "emp-tool/runtime/core/utils.h"
#include "emp-tool/runtime/crypto/ec.h"
#include <openssl/evp.h>
#include <stdio.h>
#include <cstring>

namespace emp {
class Hash { public:
	EVP_MD_CTX *mdctx;
	static const int DIGEST_SIZE = 32;
	// Application-side coalescing buffer. A tiny put() (e.g. streaming Fiat-Shamir
	// absorbing the wire byte-stream a few bytes at a time) otherwise pays
	// EVP_DigestUpdate's per-call cost, which on small inputs dwarfs the SHA work
	// (~70% pure call overhead at 6 B/put). Accumulate small puts here and hand EVP
	// one large block; flushed before any finalize so the digest is byte-identical.
	static const int BUF_BYTES = 8192;
	unsigned char buf_[BUF_BYTES];
	int buf_len_ = 0;
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
	void put(const void * data, int64_t nbyte) {
		if (nbyte <= 0) return;
		if (buf_len_ + nbyte > BUF_BYTES) flush_buf_();   // pending can't coalesce -> commit it
		if (nbyte >= BUF_BYTES) {                          // large input: straight to EVP
			if (1 != EVP_DigestUpdate(mdctx, data, nbyte))
				error("Hash::put: EVP_DigestUpdate");
		} else {                                           // small input: accumulate
			memcpy(buf_ + buf_len_, data, (size_t)nbyte);
			buf_len_ += (int)nbyte;
		}
	}
	// Commit any buffered bytes to the EVP context. Must run before every finalize
	// (digest) and is harmless to call repeatedly.
	void flush_buf_() {
		if (buf_len_) {
			if (1 != EVP_DigestUpdate(mdctx, buf_, buf_len_))
				error("Hash::flush_buf_: EVP_DigestUpdate");
			buf_len_ = 0;
		}
	}
	void put_block(const block* blk, int64_t nblock=1){
		put(blk, sizeof(block)*nblock);
	}
	// reset_after = false snapshots the running hash without disturbing
	// it: copies the EVP context, finalizes the copy, and discards it. The
	// original mdctx is untouched, so subsequent put()s continue to extend
	// the same transcript. Used for streaming Fiat-Shamir.
	void digest(void * a, bool reset_after = true) {
		flush_buf_();   // commit accumulated puts before finalizing
		if (reset_after) {
			uint32_t len = 0;
			if (1 != EVP_DigestFinal_ex(mdctx, (unsigned char *)a, &len))
				error("Hash::digest: EVP_DigestFinal_ex");
			reset();
		} else {
			EVP_MD_CTX *snap = EVP_MD_CTX_create();
			if (snap == NULL || 1 != EVP_MD_CTX_copy_ex(snap, mdctx))
				error("Hash snapshot error!");
			uint32_t len = 0;
			if (1 != EVP_DigestFinal_ex(snap, (unsigned char *)a, &len)) {
				EVP_MD_CTX_destroy(snap);
				error("Hash::digest: EVP_DigestFinal_ex (snap)");
			}
			EVP_MD_CTX_destroy(snap);
		}
	}
	void reset() {
		buf_len_ = 0;   // discard any uncommitted puts along with the EVP state
		if (1 != EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL))
			error("Hash::reset: EVP_DigestInit_ex");
	}
	static void hash_once(void * dgst, const void * data, int64_t nbyte) {
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
		uint32_t len = 0;
		if (EVP_DigestInit_ex(h.ctx, EVP_sha256(), nullptr) != 1
		    || EVP_DigestUpdate(h.ctx, data, nbyte) != 1
		    || EVP_DigestFinal_ex(h.ctx, (unsigned char *)dgst, &len) != 1)
			error("hash_once: EVP_Digest*");
	}
	#ifdef __x86_64__
	__attribute__((target("sse2")))
	#endif
	static block hash_for_block(const void * data, int64_t nbyte) {
		alignas(block) char digest[DIGEST_SIZE];
		hash_once(digest, data, nbyte);
		return _mm_load_si128((__m128i*)&digest[0]);
	}
};
}
#endif// HASH_H__
