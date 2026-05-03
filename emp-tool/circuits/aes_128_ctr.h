#ifndef EMP_AES_128_CTR_HPP__
#define EMP_AES_128_CTR_HPP__

#include "emp-tool/execution/backend.h"
#include "emp-tool/core/block.h"
#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/circuits/aes_circuit.h"

#include <openssl/evp.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>

namespace emp {

// Calculate the AES_128_CTR encryption of some bytes
// (length refers to the number of elements of type T in the array "input").
template<typename T>
int aes_128_ctr(const __m128i key,
		const __m128i iv,
		T * input, // if this is null, we'll just do a blind of length length
		uint8_t * output = nullptr, // if this is null, we'll encrypt in place
		const size_t length = 1,
		const uint64_t start_chunk = 0) {
	const size_t num_bytes = (input == nullptr) ? length : (length * sizeof(T));
	__m128i counter = iv;
	if (start_chunk != 0) { // increment iv, but it's big-endian, so it's funky
		uint64_t count;
		for(size_t i = 0; i < 8; ++i) {
			((uint8_t *)(&count))[i] = ((uint8_t *)(&counter))[15 - i];
		}
		count += start_chunk;
		for(size_t i = 0; i < 8; ++i) {
			((uint8_t *)(&counter))[15 - i] = ((uint8_t *)(&count))[i];
		}
	}
	if (input == nullptr && output == nullptr) {
		std::cerr << "input and output of aes_128_ctr can't both be null pointers\n"<<std::flush;
                return -1;
        }
	if (output == nullptr) { // then we're encrypting in place
		output = (uint8_t *) input;
	}
	if (input == nullptr) { // then we're just doing a blind
		input = (T*) output;
		for (size_t i = 0; i < num_bytes; ++i) {
			output[i] = 0;
		}
	}
	EVP_CIPHER_CTX *ctx;
	int len;
	if(!(ctx = EVP_CIPHER_CTX_new())) {
		std::cerr<< "EVP_CIPHER_CTX_new gave me a null pointer\n"<<std::flush;
		return -1;
	}
	if(1 != EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), NULL,
				(const unsigned char *) (&key), (const unsigned char *) (&counter))) {
		std::cerr<< "EVP_EncryptInit_ex gave something besides 1\n"<<std::flush;
		EVP_CIPHER_CTX_free(ctx);
		return -2;
	}
	if(1 != EVP_EncryptUpdate(ctx, (unsigned char *) output, &len, (unsigned char *) input, num_bytes)) {
		std::cerr<< "EVP_EncryptUpdate gave something besides 1\n"<<std::flush;
		EVP_CIPHER_CTX_free(ctx);
		return -3;
	}
	size_t ciphertext_len = len;
	if(1 != EVP_EncryptFinal_ex(ctx, ((unsigned char *) output) + len, &len)) {
		std::cerr<< "EVP_EncryptFinal gave something besides 1\n"<<std::flush;
		EVP_CIPHER_CTX_free(ctx);
		return -4;
	}
	ciphertext_len += len;
	EVP_CIPHER_CTX_free(ctx);
	if (ciphertext_len != num_bytes) {
		std::cerr << "ciphertext length did not end up being the correct number of bytes\n" <<std::flush;
	}
	return 0;
}

// Add `delta` to bytes 8..15 of `iv` interpreted as a big-endian uint64
// (NIST SP 800-38A counter convention: the high 8 bytes of the IV are the
// nonce, the low 8 bytes are the counter, and incrementing the counter
// increments byte 15 first). Carry beyond byte 8 is dropped — matches
// `count += start_chunk` in the CPU free function above.
template <typename Wire>
inline void ctr_inc_be64_inplace(Bit_T<Wire> iv[128], uint64_t delta) {
	if (delta == 0) return;
	// Pack iv bytes 15, 14, ..., 8 into a 64-bit LSB-first UnsignedInt
	// so adding `delta` increments byte 15 first.
	UnsignedInt_T<Wire> ctr;
	ctr.bits.resize(64);
	for (int byte = 0; byte < 8; ++byte) {
		int src_byte = 15 - byte;
		for (int k = 0; k < 8; ++k)
			ctr.bits[byte * 8 + k] = iv[src_byte * 8 + k];
	}
	ctr = ctr + UnsignedInt_T<Wire>(64, delta, PUBLIC);
	for (int byte = 0; byte < 8; ++byte) {
		int dst_byte = 15 - byte;
		for (int k = 0; k < 8; ++k)
			iv[dst_byte * 8 + k] = ctr.bits[byte * 8 + k];
	}
}

// In-circuit AES-128-CTR. Wires are typed Bit_T<Wire>. Bit / byte ordering
// is LSB-first within each byte, byte-sequential in memory — matches
// BitVec_T and the rest of emp-tool (see docs/circuits.md).
//
// CTR semantics match NIST SP 800-38A and the CPU free function above:
// the IV is treated as a 128-bit counter block where bytes 8..15 are the
// 64-bit big-endian counter that gets incremented per block.
//
// The expanded key is cached across calls — pass `key == nullptr` to
// reuse the most recently scheduled key. The first call must pass a
// non-null key.
//
// There is some internal state used during encryption, so don't try to
// use the same object in 2 threads simultaneously: just make multiple
// objects.
template<typename Wire>
class AES_128_CTR_Calculator_T { public:
	AES_128_CTR_Calculator_T() = default;

	// In-circuit key, in-circuit IV.
	int aes_128_ctr(const Bit_T<Wire> key[],
			const Bit_T<Wire> iv[],
			Bit_T<Wire> input[],
			Bit_T<Wire> * output = nullptr,
			const size_t length = 128, // in bits
			const int /*party*/ = emp::PUBLIC,
			const uint64_t start_chunk = 0) {
		Bit_T<Wire> counter_block[128];
		for (int i = 0; i < 128; ++i) counter_block[i] = iv[i];
		ctr_inc_be64_inplace<Wire>(counter_block, start_chunk);
		return run(key, counter_block, input, output, length);
	}

	// In-circuit key, out-of-circuit IV (CPU __m128i).
	int aes_128_ctr(const Bit_T<Wire> key[],
			const __m128i iv,
			Bit_T<Wire> input[],
			Bit_T<Wire> * output = nullptr,
			const size_t length = 128, // in bits
			const int party = emp::PUBLIC,
			const uint64_t start_chunk = 0) {
		// Apply start_chunk on the CPU side (gates-free), then hand off.
		__m128i counter_iv = iv;
		if (start_chunk != 0) {
			uint64_t count;
			for (size_t j = 0; j < 8; ++j)
				((uint8_t *)(&count))[j] = ((uint8_t *)(&counter_iv))[15 - j];
			count += start_chunk;
			for (size_t j = 0; j < 8; ++j)
				((uint8_t *)(&counter_iv))[15 - j] = ((uint8_t *)(&count))[j];
		}
		BitVec_T<Wire> iv_bv(128, &counter_iv, party);
		Bit_T<Wire> counter_block[128];
		for (int i = 0; i < 128; ++i) counter_block[i] = iv_bv.bits[i];
		return run(key, counter_block, input, output, length);
	}

	// Out-of-circuit key + out-of-circuit IV: the keystream is fully public,
	// so generate it on the CPU and import as a BitVec.
	int aes_128_ctr(const __m128i key,
			const __m128i iv,
			Bit_T<Wire> input[],
			Bit_T<Wire> * output = nullptr,
			const size_t length = 128, // in bits
			const int party = emp::PUBLIC,
			const uint64_t start_chunk = 0) {
		if (input == nullptr && output == nullptr) {
			std::cerr << "input and output cannot both be nullptr\n" << std::flush;
			return -1;
		}
		const size_t nbytes = (length + 7) / 8;
		uint8_t * bytes = new uint8_t[nbytes];
		int success = emp::aes_128_ctr(key, iv, (uint8_t *) nullptr, bytes, nbytes, start_chunk);
		if (success != 0) {
			delete[] bytes;
			return success;
		}
		BitVec_T<Wire> blind_int(length, bytes, party);
		if (input == nullptr) {
			for (size_t i = 0; i < length; ++i)
				output[i] = blind_int[i];
		} else {
			for (size_t i = 0; i < length; ++i) {
				Bit_T<Wire>* dst = (output == nullptr) ? &input[i] : &output[i];
				*dst = input[i] ^ blind_int[i];
			}
		}
		delete[] bytes;
		return 0;
	}

private:
	// Shared per-block CTR loop. `counter_block` already encodes the
	// starting counter (start_chunk applied). Mutates it in-place per block.
	int run(const Bit_T<Wire> key[],
	        Bit_T<Wire> counter_block[128],
	        Bit_T<Wire> input[],
	        Bit_T<Wire> * output,
	        size_t length) {
		if (input == nullptr && output == nullptr) {
			std::cerr << "input and output cannot both be nullptr\n" << std::flush;
			return -1;
		}
		if (key != nullptr) {
			aes_calc.key_schedule(key, expanded_key);
			key_set = true;
		}
		if (!key_set) {
			std::cerr << "aes_128_ctr called with null key before any key was set\n" << std::flush;
			return -2;
		}
		const bool blind_only = (input == nullptr);
		if (output == nullptr) output = input; // encrypt in place

		Bit_T<Wire> keystream[128];
		for (size_t off = 0; off < length; off += 128) {
			const size_t take = std::min<size_t>(128, length - off);
			aes_calc.encrypt(counter_block, expanded_key, keystream);
			if (blind_only) {
				for (size_t i = 0; i < take; ++i)
					output[off + i] = keystream[i];
			} else {
				for (size_t i = 0; i < take; ++i)
					output[off + i] = input[off + i] ^ keystream[i];
			}
			if (off + 128 < length)
				ctr_inc_be64_inplace<Wire>(counter_block, 1);
		}
		return 0;
	}

	AES_Calculator_T<Wire> aes_calc;
	Bit_T<Wire> expanded_key[1408];
	bool key_set = false;
};

}
#endif
