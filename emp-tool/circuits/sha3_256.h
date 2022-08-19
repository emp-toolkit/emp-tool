#ifndef EMP_SHA3_256_HPP__
#define EMP_SHA3_256_HPP__

#include "emp-tool/execution/circuit_execution.h"
#include "emp-tool/execution/protocol_execution.h"
#include "emp-tool/utils/block.h"
#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/integer.h"
#include "emp-tool/circuits/circuit_file.h"
#include <stdio.h>
#include <fstream>
#include <memory>

#include <openssl/evp.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>


extern unsigned int emp_tool_circuits_files_bristol_fashion_Keccak_f_txt_len;
extern unsigned char emp_tool_circuits_files_bristol_fashion_Keccak_f_txt[];
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
	// initialize digest engine
	if (EVP_DigestInit_ex(mdctx, algo, NULL) != 1) { // returns 1 if successful
		std::cerr << "Error in EVP_DigestInit_ex(mdctx, algo, NULL)\n" << std::flush;
		EVP_MD_CTX_destroy(mdctx);
		return -2;
	}
	// provide data to digest engine
	if (EVP_DigestUpdate(mdctx, (const uint8_t *) input, length * sizeof(T)) != 1) {
		std::cerr << "Error in EVP_DigestUpdate(mdctx, (const uint8_t *) input, length * sizeof(T))\n" << std::flush;
		EVP_MD_CTX_destroy(mdctx);
		return -3;
	}

	unsigned int digest_len = EVP_MD_size(algo);

	// produce digest
	if (EVP_DigestFinal_ex(mdctx, output, &digest_len) != 1) { // returns 1 if successful
		std::cerr << "Error in EVP_DigestFinal_ex(mdctx, output, &digest_len)\n" << std::flush;
		EVP_MD_CTX_destroy(mdctx);
		return -4;
	}

	EVP_MD_CTX_destroy(mdctx);
	return 0;
}


// Make one of these for calculating SHA3-256 hashes in circuit.
// Note that it does some setup at constructor time, so we can avoid doing that every time we hash.
// There is some internal state used during each hash, so don't try to use the same object in 2 threads simultaneously: just make multiple objects.
class SHA3_256_Calculator {
	public:
		block blocks[1600]; // internal state used during hash. Probably easier to only allocate once.
		block zero, one; // useful constants set at constructor time
		std::unique_ptr<BristolFashion> keccak_f; // ensures keccak_f is deleted when this is deleted.

		// Sets up BristolFashion circuit for calculating Keccak_f, and allocates some space and constants.
		SHA3_256_Calculator() { 
			zero = CircuitExecution::circ_exec->public_label(false);
			one = CircuitExecution::circ_exec->public_label(true);
			FILE * keccak_f_txt = fmemopen(emp_tool_circuits_files_bristol_fashion_Keccak_f_txt,
					emp_tool_circuits_files_bristol_fashion_Keccak_f_txt_len,
					"r");
			keccak_f = std::unique_ptr<BristolFashion>(new BristolFashion(keccak_f_txt));
			fclose(keccak_f_txt);
		}

		// The Keccak circuit uses different endianness, so this calculates "equivalent" addresses (in bits).
		size_t keccak_address_translator(size_t i) {
			return ((8 * (199 - (i/8))) + (i % 8));
		}

		// Calculate the sha3_256 of a bunch of blocks (essentially concatenates the arrays "inputs")
		void sha3_256(block output[], // we'll write 256 blocks to here
				const block ** inputs, // concatenate these to get the bitstring we're hashing.
				const size_t * input_sizes, // the size of each input array, please
				const size_t input_count = 1) { // the number of arrays of blocks in "inputs".
			size_t index, i, j;
			for (i = 0; i < 1600; ++i) {
				blocks[i] = zero;
			}
			index = 0;
			for (i = 0; i < input_count; ++i) {
				for (j = 0; j < input_sizes[i]; ++j) {
					blocks[keccak_address_translator(index)] =
						CircuitExecution::circ_exec->xor_gate(blocks[keccak_address_translator(index)],
								inputs[i][j]);
					++index;
					if (index == 1088) {
						keccak_f->compute(blocks, blocks);
						index = 0;
					}
				}
			}

			// I do not know why sha3 does this, but it needs to XOR the next byte with 0x06.
			index = 8 * ((index + 7)/8); // round index up to next multiple of 8
			blocks[keccak_address_translator(index + 1)] =
				CircuitExecution::circ_exec->xor_gate(blocks[keccak_address_translator(index + 1)], one);
			blocks[keccak_address_translator(index + 2)] =
				CircuitExecution::circ_exec->xor_gate(blocks[keccak_address_translator(index + 2)], one);

			// I do not know why SHA3 does this, but it does.
			blocks[519] = CircuitExecution::circ_exec->xor_gate(blocks[519], one);

			keccak_f->compute(blocks, blocks);
			for (i = 0; i < 256; ++i) {
				output[i] = blocks[keccak_address_translator(i)];
			}
		}


		// Calculate the sha3_256 of a bunch of Integers (essentially concatenates the Integers "inputs")
		void sha3_256(block output[], // we'll write 256 blocks to here as the output hash.
				const Integer inputs[], // we'll concatenate these, and hash the result.
				const size_t input_count = 1) { // the number of Integers in "inputs"
			size_t index, i, j;
			for (i = 0; i < 1600; ++i) {
				blocks[i] = zero;
			}
			index = 0;
			for (i = 0; i < input_count; ++i) {
				for (j = 0; j < inputs[i].bits.size(); ++j) {
					blocks[keccak_address_translator(index)] =
						CircuitExecution::circ_exec->xor_gate(blocks[keccak_address_translator(index)],
								inputs[i].bits[j].bit);
					++index;
					if (index == 1088) {
						keccak_f->compute(blocks, blocks);
						index = 0;
					}
				}
			}

			// I do not know why sha3 does this, but it needs to XOR the next byte with 0x06.
			index = 8 * ((index + 7)/8); // round index up to next multiple of 8
			blocks[keccak_address_translator(index + 1)] =
				CircuitExecution::circ_exec->xor_gate(blocks[keccak_address_translator(index + 1)], one);
			blocks[keccak_address_translator(index + 2)] =
				CircuitExecution::circ_exec->xor_gate(blocks[keccak_address_translator(index + 2)], one);

			// I do not know why SHA3 does this, but it does.
			blocks[519] = CircuitExecution::circ_exec->xor_gate(blocks[519], one);

			keccak_f->compute(blocks, blocks);
			for (i = 0; i < 256; ++i) {
				output[i] = blocks[keccak_address_translator(i)];
			}
		}

		// sha3_256 in circuit for an array of input blocks
		void sha3_256(block output[], // we'll write 256 blocks to here as the output hash.
				const block input[], // the array of blocks to hash (each represents a bit)
				const size_t len) { // the length of "input"
			this->sha3_256(output, &input, &len);
		}

		// Calculate the sha3_256 of a bunch of blocks (essentially concatenates the arrays "inputs")
		void sha3_256(Integer * output,// we'll write 256 bits to here as the output hash.
				const block ** inputs,// concatenate these to get the bitstring we're hashing.
				const size_t * input_sizes,// the size of each input array, please
				const size_t input_count = 1) {// the number of arrays of blocks in "inputs".
			output->bits.resize(256);
			this->sha3_256(&(output->bits[0].bit), inputs, input_sizes, input_count);
		}

		// sha3_256 in circuit for an array of input blocks
		void sha3_256(Integer * output,// we'll write 256 bits to here as the output hash.
				const block input[],// the array of blocks to hash (each represents a bit)
				const size_t len) {// the length of "input"
			output->bits.resize(256);
			this->sha3_256(&(output->bits[0].bit), input, len);
		}

		// Calculate the sha3_256 of a bunch of Integers (essentially concatenates the Integers "inputs")
		void sha3_256(Integer * output,// we'll write 256 bits to here as the output hash.
				const Integer inputs[],// we'll concatenate these, and hash the result.
				const size_t input_count = 1) {// the number of Integers in "inputs"
			output->bits.resize(256);
			this->sha3_256(&(output->bits[0].bit), inputs, input_count);
		}
};
}
#endif
