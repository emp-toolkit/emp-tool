#ifndef EMP_AES_128_CTR_HPP__
#define EMP_AES_128_CTR_HPP__

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


extern unsigned int emp_tool_circuits_files_bristol_fashion_aes_128_txt_len;
extern unsigned char emp_tool_circuits_files_bristol_fashion_aes_128_txt[];

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
	// Create and initialise the context 
	if(!(ctx = EVP_CIPHER_CTX_new())) {
		std::cerr<< "EVP_CIPHER_CTX_new gave me a null pointer\n"<<std::flush;
		return -1;
	}
	// Initialise the encryption operation. IMPORTANT - ensure you use a key
	// and IV size appropriate for your cipher
	if(1 != EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), NULL,
				(const unsigned char *) (&key), (const unsigned char *) (&counter))) {
		std::cerr<< "EVP_EncryptInit_ex gave something besides 1\n"<<std::flush;
		EVP_CIPHER_CTX_free(ctx);
		return -2;
	}
	// Provide the message to be encrypted, and obtain the encrypted output.
	// EVP_EncryptUpdate can be called multiple times if necessary
	if(1 != EVP_EncryptUpdate(ctx, (unsigned char *) output, &len, (unsigned char *) input, num_bytes)) {
		std::cerr<< "EVP_EncryptUpdate gave something besides 1\n"<<std::flush;
		EVP_CIPHER_CTX_free(ctx);
		return -3;
	}
	size_t ciphertext_len = len;
	// Finalise the encryption. Further ciphertext bytes may be written at
	// this stage.
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

// Make one of these for calculating AES_128_CTR in circuit.
// Note that it does some setup at constructor time, so we can avoid doing that every time we encrypt.
// There is some internal state used during encryption, so don't try to use the same object in 2 threads simultaneously: just make multiple objects.
class AES_128_CTR_Calculator { public:
	block keyiv[256]; // internal state used during encryption. Probably easier to only allocate once.
	block blind[128]; // internal state used during encryption. Probably easier to only allocate once.
	emp::Integer counter;
	std::unique_ptr<BristolFashion> circuit; // ensures circuit is deleted when this is deleted.

	// Sets up BristolFashion circuit for calculating aes, and allocates some space and constants.
	AES_128_CTR_Calculator() { 
		FILE * circuit_file = fmemopen(emp_tool_circuits_files_bristol_fashion_aes_128_txt,
				emp_tool_circuits_files_bristol_fashion_aes_128_txt_len,
				"r");
		this->circuit = std::unique_ptr<BristolFashion>(new BristolFashion(circuit_file));
		fclose(circuit_file);
		this->counter.bits.resize(128);
	}

	size_t reverse_bytes(const size_t i) {
		return((8 * (15 - (i / 8))) + (i % 8));
	}

	int aes_128_ctr(const block key[],
			const block iv[],
			block input[], // if this is null, we'll just do a blind of length length
			block * output = nullptr, // if this is null, we'll encrypt in place
			const size_t length = 128, // in blocks (so, basically bits)
			const int party = emp::PUBLIC,
			const uint64_t start_chunk = 0) {
		if (input == nullptr && output == nullptr) {
			std::cerr << "input and output cannot both be nullptr\n" << std::flush;
			return -1;
		}

		if (key != nullptr) {
			for (size_t i = 0; i < 128; ++i) {
				this->keyiv[i] = key[reverse_bytes(i)];
			}
		}

		if (length < 129) {

			if (start_chunk == 0) {
				for(size_t i = 0; i < 128; ++i) {
					this->keyiv[i + 128] = iv[reverse_bytes(i)];
				}
			} else {
				for(size_t i = 0; i < 128; ++i) {
					this->counter.bits[i].bit = iv[reverse_bytes(i)];
				}
				uint64_t start_chunks[2];
				start_chunks[1] = 0;
				start_chunks[0] = start_chunk;
				this->counter = this->counter + emp::Integer(128, start_chunks, party);
				for(size_t i = 0; i < 128; ++i) {
					this->keyiv[i + 128] = this->counter.bits[i].bit;
				}
			}

			if (input == nullptr) {
				this->circuit->compute(this->blind, this->keyiv);
				for(size_t i = 0; i < length; ++i) {
					output[i] = this->blind[reverse_bytes(i)];
				}
			} else {
				this->circuit->compute(this->blind, this->keyiv);
				for(size_t i = 0; i < length; ++i) {
					if (output == nullptr) {
						input[i] = CircuitExecution::circ_exec->xor_gate(input[i], this->blind[reverse_bytes(i)]);
					} else {
						output[i] = CircuitExecution::circ_exec->xor_gate(input[i], this->blind[reverse_bytes(i)]);
					}
				}
			}

		} else {
			int answer;
			for (uint64_t i = 0; (128 * i) < length; ++i) {
				answer = this->aes_128_ctr(nullptr,
						iv,
						(input == nullptr ) ? nullptr : &(input[ 128 * i]),
						(output == nullptr) ? nullptr : &(output[128 * i]),
						((128 * (i + 1)) > length) ? (length - (128 * i)) : 128,
						party,
						i + start_chunk);
				if (answer != 0) {
					return answer;
				}
			}
		}
		return 0;
	}

	int aes_128_ctr(const block key[],
			const __m128i iv, // IV here is out-of-circuit information
			block input[], // if this is null, we'll just do a blind of length length
			block * output = nullptr, // if this is null, we'll encrypt in place
			const size_t length = 128, // in blocks (so, basically bits)
			const int party = emp::PUBLIC,
			const uint64_t start_chunk = 0) {
		if (input == nullptr && output == nullptr) {
			std::cerr << "input and output cannot both be nullptr\n" << std::flush;
			return -1;
		}

		if (key != nullptr) {
			for (size_t i = 0; i < 128; ++i) {
				this->keyiv[i] = key[reverse_bytes(i)];
			}
		}

		int answer;
		__m128i counter = iv;
		uint64_t count;
		for(size_t j = 0; j < 8; ++j) {
			((uint8_t *)(&count))[j] = ((uint8_t *)(&counter))[15 - j];
		}
		count += start_chunk;

		for (uint64_t i = 0; (128 * i) < length; ++i) {
			for(size_t j = 0; j < 8; ++j) {
				((uint8_t *)(&counter))[15 - j] = ((uint8_t *)(&count))[j];
			}
			answer = this->aes_128_ctr(nullptr,
					&(emp::Integer(128, &counter, party).bits[0].bit),
					(input == nullptr ) ? nullptr : &(input[ 128 * i]),
					(output == nullptr) ? nullptr : &(output[128 * i]),
					((128 * (i + 1)) > length) ? (length - (128 * i)) : 128,
					party,
					0);
			if (answer != 0) {
				return answer;
			}
			++count;
		}

		return 0;
	}

	int aes_128_ctr(const __m128i key,// key here is out-of-circuit information
			const __m128i iv, // IV here is out-of-circuit information
			block input[], // if this is null, we'll just do a blind of length length
			block * output = nullptr, // if this is null, we'll encrypt in place
			const size_t length = 128, // in blocks (so, basically bits)
			const int party = emp::PUBLIC,
			const uint64_t start_chunk = 0) {
		if (input == nullptr && output == nullptr) {
			std::cerr << "input and output cannot both be nullptr\n" << std::flush;
			return -1;
		}

		uint8_t bytes[(length + 7) / 8];
		int success = emp::aes_128_ctr(key, iv, (uint8_t *) nullptr, bytes, (length + 7) / 8, start_chunk);
		if (success != 0) {
			return success;
		}
		emp::Integer blind = emp::Integer(length, bytes, party);

		if (input == nullptr) {
			for(size_t i = 0; i < length; ++i) {
				output[i] = blind[i].bit;
			}
		} else {
			if (output == nullptr) {
				for(size_t i = 0; i < length; ++i) {
					input[i] = CircuitExecution::circ_exec->xor_gate(input[i], blind[i].bit);
				}
			} else {
				for(size_t i = 0; i < length; ++i) {
					output[i] = CircuitExecution::circ_exec->xor_gate(input[i], blind[i].bit);
				}
			}
		}
		return 0;
	}
};
}
#endif
