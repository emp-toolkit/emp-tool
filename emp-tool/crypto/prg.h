#ifndef EMP_PRG_H__
#define EMP_PRG_H__
#include "emp-tool/core/block.h"
#include "emp-tool/crypto/aes.h"
#include "emp-tool/core/utils.h"
#include "emp-tool/core/constants.h"
#include "emp-tool/core/test_mode.h"
#include <climits>
#include <memory>
#include <random>

#ifdef ENABLE_RDSEED
#include <x86intrin.h>
#else
#endif

namespace emp {

class PRG { public:
	uint64_t counter = 0;
	AES_KEY aes;
	block key;
	PRG(const void * seed = nullptr, int id = 0) {
		if (seed != nullptr) {
			// Unaligned-safe load: caller passed a void*, so the address
			// may not meet block's 16-byte alignment. movdqu via the loadu
			// intrinsic is correct regardless; reseed then sees a true,
			// stack-aligned block*.
			block v = _mm_loadu_si128((const __m128i *)seed);
			reseed(&v, id);
			return;
		}

		block v;
		if (is_test_mode()) {
			// Deterministic seed from the global counter. Each PRG()
			// default-construction in test mode gets a distinct value
			// so streams don't collide. Single-threaded determinism
			// only — see emp-tool/core/test_mode.h.
			v = makeBlock(0LL, (int64_t)next_test_seed());
		} else {
#ifndef ENABLE_RDSEED
			v = from_urand();
#else
			unsigned long long r0, r1;
			int i = 0, j = 0;
			// To prevent an AMD CPU bug. (PR #156)
			for(; i < 10; ++i)
				if((_rdseed64_step(&r0) == 1) && (r0 != ULLONG_MAX) && (r0 != 0)) break;

			for(; j < 10; ++j)
				if((_rdseed64_step(&r1) == 1) && (r1 != ULLONG_MAX) && (r1 != 0)) break;
			if (i == 10 or j == 10)
				v = from_urand();
			else
				v = makeBlock(r0, r1);
#endif
		}
		reseed(&v, id);
	}
	block from_urand () {
		block v;
		uint32_t * data = (uint32_t *)(&v);
		std::random_device rand_div("/dev/urandom");
		for (size_t i = 0; i < sizeof(block) / sizeof(uint32_t); ++i)
			data[i] = rand_div();
		return v;
	}
	void reseed(const block* seed, uint64_t id = 0) {
		block v = *seed;
		v ^= makeBlock(0LL, id);
		key = v;
		AES_set_encrypt_key(v, &aes);
		counter = 0;
	}

	// `data` must be block-aligned (16-byte). Use random_data_unaligned otherwise.
	void random_data(void *data, int64_t nbytes) {
		assert(((uintptr_t)data & (alignof(block) - 1)) == 0 &&
		       "random_data requires 16-byte aligned data; use random_data_unaligned");
		random_block((block *)data, nbytes/16);
		if (nbytes % 16 != 0) {
			block extra;
			random_block(&extra, 1);
			memcpy((nbytes/16*16)+(char *) data, &extra, nbytes%16);
		}
	}

	// Unpacks 8 bools per random byte (= 128 bools per AES block) instead of
	// consuming a full byte for one bit, an 8x cut in AES work. Inner unpack
	// uses bits32_to_bytes (SIMD) to expand 4 bytes → 32 bools per call.
	void random_bool(bool * data, int64_t length) {
		if (length <= 0) return;
		constexpr int CHUNK_B = 16;  // 16 blocks = 2048 bits per pass
		block buf[CHUNK_B];
		int64_t produced = 0;
		while (produced < length) {
			int64_t remaining = length - produced;
			int64_t bits_pass = remaining < CHUNK_B * 128 ? remaining : CHUNK_B * 128;
			int64_t blocks_pass = (bits_pass + 127) / 128;
			random_block(buf, blocks_pass);
			const uint8_t *bytes = reinterpret_cast<const uint8_t *>(buf);
			int64_t full32 = bits_pass / 32;
			for (int64_t i = 0; i < full32; ++i) {
				uint32_t b32;
				memcpy(&b32, bytes + i * 4, 4);
				bits32_to_bytes(b32, data + produced + i * 32);
			}
			produced += full32 * 32;
			int64_t tail_bits = bits_pass - full32 * 32;
			if (tail_bits > 0) {
				uint32_t b32 = 0;
				memcpy(&b32, bytes + full32 * 4, (tail_bits + 7) / 8);
				bool tmp[32];
				bits32_to_bytes(b32, tmp);
				memcpy(data + produced, tmp, tail_bits);
				produced += tail_bits;
			}
		}
	}

    void random_data_unaligned(void *data, int64_t nbytes) {
        if (nbytes <= 0) return;
        // Small-buffer fast path. Anything that fits in two blocks is
        // filled with one random_block draw and copied — no alignment
        // dance. This also covers every case where std::align below
        // could fail (failure requires nbytes <= 30, since the worst
        // alignment skew is 15 bytes and the aligned region needs 16).
        if (nbytes <= (int64_t)(2 * sizeof(block))) {
            block tmp[2];
            random_block(tmp, 2);
            memcpy(data, tmp, nbytes);
            return;
        }

        // Aligned bulk path. nbytes > 32 here, so std::align is
        // guaranteed to succeed.
        size_t size = nbytes;
        void *aligned_data = data;
        std::align(sizeof(block), sizeof(block), aligned_data, size);
        // round down to a whole number of blocks
        size = sizeof(block) * (size / sizeof(block));
        int64_t chopped = nbytes - size;

        // temporarily fill the bulk of the buffer with random data
        random_data(aligned_data, nbytes - chopped);

        // move the random data to the start of the buffer
        // (using memmove, not memcpy, because of memory overlap)
        memmove(data, aligned_data, nbytes - chopped);

        int64_t remaining_bytes = chopped;
        char* end = (char*)data + nbytes;

        // there can be 0-2 blocks leftover
        // eg: <- 8 bytes -> <- N blocks -> <- 15 bytes ->
        //     in the above case, the N blocks are filled by random_data,
        //     leaving 23 bytes leftover, which requires 2 blocks to fill,
        //     of which we must ignore 9 bytes to avoid writing past the
        //     end of the buffer (`void *data`).
        while (remaining_bytes > 0) {
            block tmp;
            random_block(&tmp, 1);
            int64_t bytes_to_copy = std::min(remaining_bytes, (int64_t)sizeof(block));
            memcpy(end - remaining_bytes, &tmp, bytes_to_copy);
            remaining_bytes -= bytes_to_copy;
        }
    }

	void random_block(block * data, int64_t nblocks=1) {
		// Caller must pass block-aligned `data` (16 bytes). random_data and
		// random_data_unaligned wrap unaligned inputs.
		//
		// AES_BATCH_SIZE keeps the AES-encrypt phase's per-call setup
		// (round-key broadcasts on the VAES path) amortized over many
		// blocks; ParaCtrEnc builds counter plaintexts in-register and
		// writes only the encrypted output to `data` — no intermediate
		// counter-write pass.
		assert(((uintptr_t)data & (alignof(block) - 1)) == 0 &&
		       "random_block requires 16-byte aligned data");
		while (nblocks > 0) {
			int64_t n = nblocks < AES_BATCH_SIZE ? nblocks : AES_BATCH_SIZE;
			detail::ParaCtrEnc(data, (int64_t)counter, &aes, n);
			counter += n;
			data += n;
			nblocks -= n;
		}
	}

	typedef uint64_t result_type;
	// alignas(block): operator() reinterprets buffer as block* and feeds
	// random_block, which requires 16-byte alignment. Without alignas the
	// buffer's offset within PRG happens to be 16-aligned today, but a
	// future reorder of PRG members could silently break that.
	alignas(block) result_type buffer[32];
	size_t ptr = 32;
	static constexpr result_type min() {
		return 0;
	}
	static constexpr result_type max() {
		return 0xFFFFFFFFFFFFFFFFULL;
	}
	result_type operator()() {
		if(ptr == 32) {		
			random_block((block*)buffer, 16);
			ptr = 0;
		}
		return buffer[ptr++];
	}
};

}
#endif// PRP_H__
