#ifndef EMP_SHA3_256_HPP__
#define EMP_SHA3_256_HPP__

#include "emp-tool/execution/backend.h"
#include "emp-tool/core/block.h"
#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/sha3_circuit.h"

#include <openssl/evp.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace emp {

// Calculate the sha3 hash of arbitrary bytes in memory using OpenSSL.
// (length refers to the number of elements of type T in the array "input".
// Beware: ensure the array "output" can hold at least 32 bytes.
// returns 0 if all went well, negative numbers otherwise.
template<typename T>
int sha3_256(uint8_t * output, const T * input, const size_t length = 1) {
	EVP_MD_CTX * mdctx;
	const EVP_MD * algo = EVP_sha3_256();

	if ((mdctx = EVP_MD_CTX_create()) == NULL) {
		std::cerr << "Error in EVP_MD_CTX_create()\n" << std::flush;
		return -1;
	}
	if (EVP_DigestInit_ex(mdctx, algo, NULL) != 1) {
		std::cerr << "Error in EVP_DigestInit_ex(mdctx, algo, NULL)\n" << std::flush;
		EVP_MD_CTX_destroy(mdctx);
		return -2;
	}
	if (EVP_DigestUpdate(mdctx, (const uint8_t *) input, length * sizeof(T)) != 1) {
		std::cerr << "Error in EVP_DigestUpdate(mdctx, (const uint8_t *) input, length * sizeof(T))\n" << std::flush;
		EVP_MD_CTX_destroy(mdctx);
		return -3;
	}

	unsigned int digest_len = EVP_MD_size(algo);

	if (EVP_DigestFinal_ex(mdctx, output, &digest_len) != 1) {
		std::cerr << "Error in EVP_DigestFinal_ex(mdctx, output, &digest_len)\n" << std::flush;
		EVP_MD_CTX_destroy(mdctx);
		return -4;
	}

	EVP_MD_CTX_destroy(mdctx);
	return 0;
}


// In-circuit SHA3-256. Wires are typed Bit_T<Wire>; the wire type is
// defined by the active backend. Bit / byte ordering is LSB-first within
// each byte, byte-sequential — matches BitVec_T and the rest of emp-tool
// (see docs/circuits.md).
//
// Internally calls Keccak_F_Calculator_T<Wire>::permute on a 1600-bit state
// laid out per FIPS-202 §B.1: input byte j, bit k (k=0=LSB) ends up at
//   state_index = 64*(j/8) + 8*(j%8) + k.
// (lane_idx = j/8 unpacks to (x, y) = (lane_idx%5, lane_idx/5).)
//
// There is some internal state used during each hash, so don't try to use
// the same object in 2 threads simultaneously: just make multiple objects.
template<typename Wire>
class SHA3_256_Calculator_T {
	public:
		Bit_T<Wire> blocks[1600]; // internal sponge state
		Bit_T<Wire> zero, one;    // useful constants set at constructor time
		Keccak_F_Calculator_T<Wire> kf;

		SHA3_256_Calculator_T() {
			zero = Bit_T<Wire>(false, PUBLIC);
			one  = Bit_T<Wire>(true,  PUBLIC);
		}

		// Map an input bit index (LSB-first within byte, byte-sequential)
		// to the corresponding bit position in the FIPS-202 1600-bit state.
		static inline size_t fips202_bit_index(size_t input_bit_idx) {
			const size_t byte_j   = input_bit_idx / 8;
			const size_t bit_k    = input_bit_idx % 8;
			const size_t lane_idx = byte_j / 8;
			const size_t x        = lane_idx % 5;
			const size_t y        = lane_idx / 5;
			const size_t z        = 8 * (byte_j % 8) + bit_k;
			return 64 * (5 * y + x) + z;
		}

		// Calculate the sha3_256 of a bunch of wire arrays (essentially concatenates the arrays "inputs")
		void sha3_256(Bit_T<Wire> output[], // we'll write 256 bits to here
				const Bit_T<Wire> ** inputs, // concatenate these to get the bitstring we're hashing.
				const size_t * input_sizes, // the size of each input array, please
				const size_t input_count = 1) { // the number of arrays in "inputs".
			for (size_t i = 0; i < 1600; ++i) blocks[i] = zero;

			size_t index = 0;
			for (size_t i = 0; i < input_count; ++i) {
				for (size_t j = 0; j < input_sizes[i]; ++j) {
					const size_t s = fips202_bit_index(index);
					blocks[s] = blocks[s] ^ inputs[i][j];
					++index;
					if (index == 1088) {
						kf.permute(blocks);
						index = 0;
					}
				}
			}

			finalize_and_squeeze(index, output);
		}


		// Calculate the sha3_256 of a bunch of Integers (essentially concatenates the Integers "inputs")
		void sha3_256(Bit_T<Wire> output[], // we'll write 256 bits to here as the output hash.
				const BitVec_T<Wire> inputs[], // we'll concatenate these, and hash the result.
				const size_t input_count = 1) { // the number of Integers in "inputs"
			for (size_t i = 0; i < 1600; ++i) blocks[i] = zero;

			size_t index = 0;
			for (size_t i = 0; i < input_count; ++i) {
				for (size_t j = 0; j < inputs[i].bits.size(); ++j) {
					const size_t s = fips202_bit_index(index);
					blocks[s] = blocks[s] ^ inputs[i].bits[j];
					++index;
					if (index == 1088) {
						kf.permute(blocks);
						index = 0;
					}
				}
			}

			finalize_and_squeeze(index, output);
		}

		// sha3_256 in circuit for an array of input wires
		void sha3_256(Bit_T<Wire> output[], // we'll write 256 bits to here as the output hash.
				const Bit_T<Wire> input[], // the array of bits to hash
				const size_t len) { // the length of "input"
			const Bit_T<Wire>* p = input;
			this->sha3_256(output, &p, &len);
		}

		// Calculate the sha3_256 of a bunch of wire arrays (essentially concatenates the arrays "inputs")
		void sha3_256(BitVec_T<Wire> * output,// we'll write 256 bits to here as the output hash.
				const Bit_T<Wire> ** inputs,// concatenate these to get the bitstring we're hashing.
				const size_t * input_sizes,// the size of each input array, please
				const size_t input_count = 1) {// the number of arrays in "inputs".
			output->bits.resize(256);
			this->sha3_256(output->bits.data(), inputs, input_sizes, input_count);
		}

		// sha3_256 in circuit for an array of input wires
		void sha3_256(BitVec_T<Wire> * output,// we'll write 256 bits to here as the output hash.
				const Bit_T<Wire> input[],// the array of bits to hash
				const size_t len) {// the length of "input"
			output->bits.resize(256);
			this->sha3_256(output->bits.data(), input, len);
		}

		// Calculate the sha3_256 of a bunch of Integers (essentially concatenates the Integers "inputs")
		void sha3_256(BitVec_T<Wire> * output,// we'll write 256 bits to here as the output hash.
				const BitVec_T<Wire> inputs[],// we'll concatenate these, and hash the result.
				const size_t input_count = 1) {// the number of Integers in "inputs"
			output->bits.resize(256);
			this->sha3_256(output->bits.data(), inputs, input_count);
		}

	private:
		// Apply SHA3 padding (domain separator 0x06 + pad10*1 trailing 0x80)
		// for a partial-block index in [0, 1088), then permute and squeeze
		// the first 256 bits of state into `output`.
		void finalize_and_squeeze(size_t index, Bit_T<Wire> output[]) {
			// Round index up to next byte boundary, then XOR 0x06 at that
			// byte (bits 1 and 2 set, LSB-first = 0b00000110 = 0x06).
			index = 8 * ((index + 7) / 8);
			{
				const size_t s1 = fips202_bit_index(index + 1);
				const size_t s2 = fips202_bit_index(index + 2);
				blocks[s1] = !blocks[s1];
				blocks[s2] = !blocks[s2];
			}

			// Final 1 of pad10*1 at byte 135 (last rate byte), bit 7 = 0x80.
			// fips202_bit_index(1087) = 1087.
			blocks[1087] = !blocks[1087];

			kf.permute(blocks);

			for (size_t i = 0; i < 256; ++i)
				output[i] = blocks[fips202_bit_index(i)];
		}
};

}  // namespace emp
#endif
