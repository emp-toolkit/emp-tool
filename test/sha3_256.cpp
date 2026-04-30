#include "emp-tool/emp-tool.h"
#include <iostream>

using namespace std;
using namespace emp;

// try hashing a fairly arbitrary byte string and see if we get the right value.
int hash_in_circuit(){

  uint8_t input[2000];
  uint8_t output_bytes[32];
  uint8_t output_bytes2[32];
  for (size_t i = 0; i < 2000; ++i) {
    input[i] = i % 200;
  }
  emp::sha3_256(output_bytes, input, 2000);

  emp::BitVec integers[2000];
  for (int64_t i = 0; i < 2000; ++i) {
    integers[i] = BitVec(8, (uint64_t)(i % 200), emp::PUBLIC);
  }

  emp::BitVec output(10, (uint32_t)32, emp::PUBLIC);

  SHA3_256_Calculator sha3_256_calculator;
  sha3_256_calculator.sha3_256(&output, integers, 2000);
  output.reveal(output_bytes2, PUBLIC);

  for(uint8_t i=0; i<32; ++i) {
    if (output_bytes[i] != output_bytes2[i]) {
      std::cerr << "sha3 hash did not produce the correct hash value\n" << std::flush;
      return -1;
    }
  }
  return 0;
}

int main(int argc, char** argv) {
	setup_clear_backend();
	hash_in_circuit();
	finalize_clear_backend();
	return 0;
}
