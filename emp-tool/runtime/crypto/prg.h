#ifndef EMP_PRG_H__
#define EMP_PRG_H__
#include "emp-tool/runtime/core/block.h"
#include "emp-tool/runtime/crypto/aes.h"
#include "emp-tool/runtime/core/utils.h"
#include "emp-tool/runtime/core/constants.h"
#include "emp-tool/runtime/core/deterministic_mode.h"
#include <memory>
#include <sys/random.h>

namespace emp {

class PRG { public:
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
		if constexpr (is_deterministic_mode()) {
			// Deterministic seed: the party's lane in the high half, the
			// per-lane construction ordinal in the low half. A lane's
			// sequence depends only on its own program order, so
			// multi-threaded runs reproduce under the lane discipline of
			// emp-tool/runtime/core/deterministic_mode.h.
			const DeterministicSeed ds = next_deterministic_seed();
			v = makeBlock((int64_t)deterministic_seed_high(ds), (int64_t)ds.ordinal);
		} else {
			v = from_urand();
		}
		reseed(&v, id);
	}
	block from_urand () {
		// Kernel entropy. Seeding sits on the exception-free public
		// surface (docs/api_conventions.md), so the (practically
		// unreachable) failure path fail-stops.
		block v;
		expecting(getentropy(&v, sizeof(block)) == 0,
		          "PRG: system entropy unavailable");
		return v;
	}
	void reseed(const block* seed, uint64_t id = 0) {
		block v = *seed;
		v ^= makeBlock(0LL, id);
		key = v;
		AES_set_encrypt_key(v, &aes);
		counter = 0;
		ptr = 32;
	}

	// Stream position is the CTR block counter. seed() is the effective key
	// (post id-XOR); a fresh PRG built from it and seek()'d to position c
	// reproduces this PRG's output from block c onward — see fork_at().
	block seed() const { return key; }
	uint64_t position() const { return counter; }
	void seek(uint64_t block_counter) {
		counter = block_counter;
		ptr = 32;  // invalidate the buffered operator() words
	}
	PRG fork_at(uint64_t block_counter) const {
		PRG p(*this);  // copy shares the AES key; no re-schedule
		p.seek(block_counter);
		return p;
	}

	// `data` must be block-aligned (16-byte). Use random_data_unaligned otherwise.
	void random_data(void *data, int64_t nbytes) {
		expecting(nbytes >= 0, "PRG::random_data: negative byte count");
		if (nbytes == 0) return;
		expecting(((uintptr_t)data & (alignof(block) - 1)) == 0,
		          "PRG::random_data: data must be 16-byte aligned; use random_data_unaligned");
		fill_bytes_(static_cast<char *>(data), nbytes);
	}

	// Unpacks 8 bools per random byte (= 128 bools per AES block). The inner
	// unpack uses bits32_to_bytes (SIMD) to expand 4 bytes → 32 bools per call.
	void random_bool(bool * data, int64_t length) {
		random_bool_impl_(data, length);
	}

	template <typename T>
	requires std::is_same_v<T, uint8_t>
	void random_bool(T *data, int64_t length) {
		random_bool_impl_(data, length);
	}

	// Any alignment; the same bytes, and the same stream advance, as
	// random_data on an aligned buffer.
	void random_data_unaligned(void *data, int64_t nbytes) {
		expecting(nbytes >= 0,
		          "PRG::random_data_unaligned: negative byte count");
		fill_bytes_(static_cast<char *>(data), nbytes);
	}

	void random_block(block * data, int64_t nblocks=1) {
		expecting(nblocks >= 0, "PRG::random_block: negative block count");
		if (nblocks == 0) return;
		// Caller must pass block-aligned `data` (16 bytes). random_data_unaligned
		// takes any alignment.
		expecting(((uintptr_t)data & (alignof(block) - 1)) == 0,
		          "PRG::random_block: data must be 16-byte aligned");
		fill_blocks_(data, nblocks);
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

private:
	// The next n blocks of the stream, written to `out` at any alignment by one
	// call into the CTR kernel (its stores are unaligned on every tier), which
	// builds the counters in registers and writes only the output.
	void fill_blocks_(block *out, int64_t n) {
		detail::ParaCtrEnc(out, counter, &aes, n);
		counter += static_cast<uint64_t>(n);
	}

	// The next nbytes of the stream, at any alignment of `out`: the whole
	// blocks straight from the CTR kernel, then the first nbytes % 16 bytes of
	// one more block. The stream advances by ceil(nbytes / 16) blocks.
	void fill_bytes_(char *out, int64_t nbytes) {
		const int64_t whole = nbytes / static_cast<int64_t>(sizeof(block));
		fill_blocks_(reinterpret_cast<block *>(out), whole);
		if (const int64_t tail = nbytes % static_cast<int64_t>(sizeof(block))) {
			block extra;
			fill_blocks_(&extra, 1);
			memcpy(out + whole * static_cast<int64_t>(sizeof(block)), &extra,
			       static_cast<size_t>(tail));
		}
	}

	template <typename T>
	void random_bool_impl_(T *data, int64_t length) {
		expecting(length >= 0, "PRG::random_bool: negative bit count");
		if (length == 0) return;
		constexpr int CHUNK_B = 16;
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
				for (int64_t i = 0; i < tail_bits; ++i)
					data[produced + i] = static_cast<T>((b32 >> i) & 1);
				produced += tail_bits;
			}
		}
	}

	uint64_t counter = 0;
	AES_KEY aes;
	block key;
};

}
#endif// PRG_H__
