#ifndef EMP_HASH_H__
#define EMP_HASH_H__

#include "emp-tool/runtime/core/block.h"
#include "emp-tool/runtime/core/utils.h"
#include "emp-tool/runtime/crypto/ec.h"
#include <openssl/evp.h>
#include <stdio.h>
#include <cstring>
#include <type_traits>

// One Hash templated on the algorithm. Default is OpenSSL SHA-256 (production);
// a build opts into the vendored BLAKE3 (third_party/blake3, ~3-4x faster per
// byte) with -DEMP_WITH_BLAKE3 and, to flip the default, -DEMP_HASH_DEFAULT.
// Switching the algorithm changes the Fiat-Shamir / commitment transcript, so
// it is a protocol-wide choice inherited by every consumer of this build.
#ifdef EMP_WITH_BLAKE3
  #include "blake3.h"
#endif

namespace emp {

enum class HashOption { sha256, blake3 };

#ifndef EMP_HASH_DEFAULT
#define EMP_HASH_DEFAULT ::emp::HashOption::sha256
#endif

#ifdef EMP_WITH_BLAKE3
// Build-consistency backstop. With both algorithms compiled in, every library
// linked together MUST agree on EMP_HASH_DEFAULT: the default Hash's layout
// differs by algorithm (EVP_MD_CTX* vs blake3_hasher) and its objects cross the
// emp-tool / emp-ot / emp-ag boundary (the IOChannel Fiat-Shamir Hash). The CMake
// build guarantees agreement — consumers inherit the define from emp-tool's
// exported interface — so this only fires on a hand-mismatched or stale-lib build,
// turning it into a link error ("undefined reference to emp::HashAbiMarker<...>")
// instead of a silent Hash-layout corruption. libemp-tool defines the marker for
// the option it was built with; every TU references the marker for its own.
template <HashOption> struct HashAbiMarker { static const char sym; };
namespace detail {
[[maybe_unused, gnu::used]] static const char *const emp_hash_abi_guard =
    &HashAbiMarker<EMP_HASH_DEFAULT>::sym;
}
#endif

template <HashOption opt = EMP_HASH_DEFAULT>
class HashT { public:
#ifdef EMP_WITH_BLAKE3
	using state_t = std::conditional_t<opt == HashOption::blake3, blake3_hasher, EVP_MD_CTX*>;
#else
	static_assert(opt == HashOption::sha256,
	              "rebuild with -DEMP_WITH_BLAKE3 to use HashOption::blake3");
	using state_t = EVP_MD_CTX*;
#endif
	static const int DIGEST_SIZE = 32;
	// Application-side coalescing buffer. A tiny put() (e.g. streaming Fiat-Shamir
	// absorbing the wire byte-stream a few bytes at a time) otherwise pays the
	// per-call cost, which on small inputs dwarfs the hash work. Accumulate small
	// puts here and hand the algorithm one large block; flushed before any
	// finalize so the digest is byte-identical to an un-buffered absorb.
	static const int BUF_BYTES = 8192;
	state_t st_;
	unsigned char buf_[BUF_BYTES];
	int buf_len_ = 0;
	HashT() {
		if constexpr (opt == HashOption::blake3) {
#ifdef EMP_WITH_BLAKE3
			blake3_hasher_init(&st_);
#endif
		} else {
			if ((st_ = EVP_MD_CTX_create()) == NULL) error("Hash function setup error!");
			if (1 != EVP_DigestInit_ex(st_, EVP_sha256(), NULL)) error("Hash function setup error!");
		}
	}
	~HashT() {
		if constexpr (opt == HashOption::sha256) EVP_MD_CTX_destroy(st_);
	}
	// Owns a hash context; copying would double-free / desync on dtor.
	HashT(const HashT&) = delete;
	HashT& operator=(const HashT&) = delete;
	void put(const void * data, int64_t nbyte) {
		if (nbyte <= 0) return;
		if (buf_len_ + nbyte > BUF_BYTES) flush_buf_();   // pending can't coalesce -> commit it
		if (nbyte >= BUF_BYTES) update_(data, nbyte);      // large input: straight to the algorithm
		else { memcpy(buf_ + buf_len_, data, (size_t)nbyte); buf_len_ += (int)nbyte; }  // small: accumulate
	}
	// Commit any buffered bytes. Must run before every finalize; harmless to repeat.
	void flush_buf_() {
		if (buf_len_) { update_(buf_, buf_len_); buf_len_ = 0; }
	}
	void put_block(const block* blk, int64_t nblock=1){
		put(blk, sizeof(block)*nblock);
	}
	// reset_after = false snapshots the running hash without disturbing it, so
	// subsequent put()s continue the same transcript (streaming Fiat-Shamir).
	// SHA-256 copies the EVP context for the snapshot; BLAKE3's finalize is
	// already const (non-destructive), so no copy is needed.
	void digest(void * a, bool reset_after = true) {
		flush_buf_();
		if constexpr (opt == HashOption::blake3) {
#ifdef EMP_WITH_BLAKE3
			blake3_hasher_finalize(&st_, (uint8_t *)a, DIGEST_SIZE);
			if (reset_after) blake3_hasher_reset(&st_);
#endif
		} else {
			uint32_t len = 0;
			if (reset_after) {
				if (1 != EVP_DigestFinal_ex(st_, (unsigned char *)a, &len))
					error("Hash::digest: EVP_DigestFinal_ex");
				if (1 != EVP_DigestInit_ex(st_, EVP_sha256(), NULL))
					error("Hash::digest: EVP_DigestInit_ex");
			} else {
				EVP_MD_CTX *snap = EVP_MD_CTX_create();
				if (snap == NULL || 1 != EVP_MD_CTX_copy_ex(snap, st_)) error("Hash snapshot error!");
				if (1 != EVP_DigestFinal_ex(snap, (unsigned char *)a, &len)) {
					EVP_MD_CTX_destroy(snap);
					error("Hash::digest: EVP_DigestFinal_ex (snap)");
				}
				EVP_MD_CTX_destroy(snap);
			}
		}
	}
	void reset() {
		buf_len_ = 0;
		if constexpr (opt == HashOption::blake3) {
#ifdef EMP_WITH_BLAKE3
			blake3_hasher_reset(&st_);
#endif
		} else {
			if (1 != EVP_DigestInit_ex(st_, EVP_sha256(), NULL)) error("Hash::reset: EVP_DigestInit_ex");
		}
	}
	static void hash_once(void * dgst, const void * data, int64_t nbyte) {
		if constexpr (opt == HashOption::blake3) {
#ifdef EMP_WITH_BLAKE3
			thread_local blake3_hasher t;
			blake3_hasher_init(&t);
			blake3_hasher_update(&t, data, (size_t)nbyte);
			blake3_hasher_finalize(&t, (uint8_t *)dgst, DIGEST_SIZE);
#endif
		} else {
			// Per-thread persistent EVP_MD_CTX. The dominant cost of a transient
			// context on small inputs is EVP_MD_CTX_new/free (~700-1000 cy each);
			// reusing one drops that to a single EVP_DigestInit_ex.
			struct Holder {
				EVP_MD_CTX * c;
				Holder() : c(EVP_MD_CTX_new()) { if (!c) error("hash_once: EVP_MD_CTX_new failed"); }
				~Holder() { if (c) EVP_MD_CTX_free(c); }
			};
			thread_local Holder h;
			uint32_t len = 0;
			if (EVP_DigestInit_ex(h.c, EVP_sha256(), nullptr) != 1
			    || EVP_DigestUpdate(h.c, data, nbyte) != 1
			    || EVP_DigestFinal_ex(h.c, (unsigned char *)dgst, &len) != 1)
				error("hash_once: EVP_Digest*");
		}
	}
	#ifdef __x86_64__
	__attribute__((target("sse2")))
	#endif
	static block hash_for_block(const void * data, int64_t nbyte) {
		alignas(block) char digest[DIGEST_SIZE];
		hash_once(digest, data, nbyte);
		return _mm_load_si128((__m128i*)&digest[0]);
	}
private:
	void update_(const void * data, int64_t nbyte) {
		if constexpr (opt == HashOption::blake3) {
#ifdef EMP_WITH_BLAKE3
			blake3_hasher_update(&st_, data, (size_t)nbyte);
#endif
		} else {
			if (1 != EVP_DigestUpdate(st_, data, nbyte)) error("Hash::put: EVP_DigestUpdate");
		}
	}
};

// Default-algorithm alias so the whole codebase keeps writing `Hash` unchanged;
// a site that wants to pin an algorithm names HashT<HashOption::blake3>.
using Hash = HashT<>;

}
#endif// HASH_H__
