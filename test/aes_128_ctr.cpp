#include "emp-tool/emp-tool.h"
#include <iostream>

using namespace std;
using namespace emp;


// try hashing a fairly arbitrary byte string and see if we get the right value.
int hash_in_circuit(){

  uint8_t input[2000];
  uint8_t output_bytes[2000];
  uint8_t output_bytes2[2000];
  uint8_t decrypted_bytes[2000];
  for (size_t i = 0; i < 2000; ++i) {
    input[i] = i % 200;
  }
  __m128i key;
  __m128i iv;
  for (size_t i = 0; i < 16; ++i) {
    ((uint8_t *)(&key))[i] = (1337 * i) % 255;
    ((uint8_t *)(&iv))[i] = (31 * i) % 253;
  }


  emp::aes_128_ctr(key, iv, input, output_bytes, 2000, 77777);

  // let's make sure we can decrypt this
  emp::aes_128_ctr(key, iv, output_bytes, decrypted_bytes, 2000, 77777);
  for(size_t i=0; i<2000; ++i) {
    if (input[i] != decrypted_bytes[i]) {
      std::cerr << "decryption did not match input\n" << std::flush;
      return -1;
    }
  }

  std::cout << "in memory:  ";
  for (size_t i = 0; i < 32; ++i) {
    std::cout << std::setw(2) << std::setfill('0') << std::hex << (int)(output_bytes[1000 + i]) << " ";
  }


  // now to do the same thing in circuit
  emp::AES_128_CTR_Calculator aes_128_ctr_calculator = emp::AES_128_CTR_Calculator();
  emp::Integer input_integer = emp::Integer(2000 * 8, input, emp::PUBLIC);
  emp::Integer output_integer = emp::Integer(2000 * 8, input, emp::PUBLIC);
  emp::Integer iv_integer = emp::Integer(128, &iv, emp::PUBLIC);
  emp::Integer key_integer = emp::Integer(128, &key, emp::PUBLIC);

  aes_128_ctr_calculator.aes_128_ctr(&(key_integer[0].bit),
                                     &(iv_integer[0].bit),
                                     &(input_integer[0].bit),
                                     &(output_integer[0].bit),
                                     2000 * 8,
                                     emp::PUBLIC,
                                     77777);

  output_integer.reveal<uint8_t>(output_bytes2, PUBLIC);
  std::cout << "\nin circuit: ";
  for (size_t i = 0; i < 32; ++i) {
    std::cout << std::setw(2) << std::setfill('0') << std::hex << (int)(output_bytes2[1000 + i]) << " ";
  }

  // let's make sure the circuit output matches the in-memory output.
  for(size_t i=0; i<2000; ++i) {
    if (output_bytes[i] != output_bytes2[i]) {
      std::cerr << "aes did not match in and out of circuit\n" << std::flush;
      return -1;
    }
  }

  // now with the out-of-circuit IV.
  aes_128_ctr_calculator.aes_128_ctr(&(key_integer[0].bit),
                                     iv,
                                     &(input_integer[0].bit),
                                     &(output_integer[0].bit),
                                     2000 * 8,
                                     emp::PUBLIC,
                                     77777);

  output_integer.reveal<uint8_t>(output_bytes2, PUBLIC);
  std::cout << "\nin circuit2:";
  for (size_t i = 0; i < 32; ++i) {
    std::cout << std::setw(2) << std::setfill('0') << std::hex << (int)(output_bytes2[1000 + i]) << " ";
  }

  // let's make sure the circuit output matches the in-memory output.
  for(size_t i=0; i<2000; ++i) {
    if (output_bytes[i] != output_bytes2[i]) {
      std::cerr << "aes did not match in and out of circuit\n" << std::flush;
      return -1;
    }
  }

  // now with the out-of-circuit key and IV.
  aes_128_ctr_calculator.aes_128_ctr(key,
                                     iv,
                                     &(input_integer[0].bit),
                                     &(output_integer[0].bit),
                                     2000 * 8,
                                     emp::PUBLIC,
                                     77777);

  output_integer.reveal<uint8_t>(output_bytes2, PUBLIC);
  std::cout << "\nin circuit3:";
  for (size_t i = 0; i < 32; ++i) {
    std::cout << std::setw(2) << std::setfill('0') << std::hex << (int)(output_bytes2[1000 + i]) << " ";
  }
  std::cout << "\n";

  // let's make sure the circuit output matches the in-memory output.
  for(size_t i=0; i<2000; ++i) {
    if (output_bytes[i] != output_bytes2[i]) {
      std::cerr << "aes did not match in and out of circuit\n" << std::flush;
      return -1;
    }
  }



  return 0;
}

int main(int argc, char** argv) {
	setup_plain_prot(false, "");
	hash_in_circuit();
	finalize_plain_prot();
	return 0;
}
