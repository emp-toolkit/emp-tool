#include "emp-tool/emp-tool.h"
#include <iostream>
#include <emp-tool/circuits/sha3_256.hpp>

using namespace std;
using namespace emp;

int port, party;

void hash_in_circuit(){

  emp::Integer integers[200];
  for (int64_t i = 0; i < 200; ++i) {
    integers[i] = Integer(8, i % 100, emp::PUBLIC);
  }

  emp::Integer output = Integer(10, 32, emp::PUBLIC);

  std::cout << "\ngenerating calculator\n" << std::flush;
  SHA3_256_Calculator sha3_256_calculator = SHA3_256_Calculator();
  std::cout << "generated.\ncalculating hash\n" << std::flush;
  sha3_256_calculator.sha3_256(&output, integers, 200);
  std::cout << "calculated.\n" << std::flush;
  // As of 2021-04-16,
  // Whenever I try to reveal a block, I get a segfault.
  // This isn't the case when I run this in, say, the semi-honest environment.
  // So I assume this is an environment issue.

}
template<typename T>
void test(T * netio) {
	if(party == BOB) {
		HalfGateEva<T>::circ_exec = new HalfGateEva<T>(netio);
    hash_in_circuit();
		delete HalfGateEva<T>::circ_exec;
	} else {
		HalfGateGen<T>::circ_exec = new HalfGateGen<T>(netio);
    hash_in_circuit();
		delete HalfGateGen<T>::circ_exec;
	}
}
int main(int argc, char** argv) {

  uint8_t input[200];
  uint8_t output[32];
  for (uint8_t i = 0; i < 200; ++i) {
    input[i] = i % 100;
  }
  emp::sha3_256(output, input, 200);
  for(uint8_t i=0; i<32; ++i) {
    std::cout << std::setw(2) << std::setfill('0') << std::hex << (int)output[i];
  }
  std::cout << "\n";





	parse_party_and_port(argv, &party, &port);
	cout <<"Using NetIO\n";
	NetIO* netio = new NetIO(party == ALICE?nullptr:"127.0.0.1", port);
	test<NetIO>(netio);
	delete netio;

	cout <<"Using HighSpeedNetIO\n";
	HighSpeedNetIO* hsnetio = new HighSpeedNetIO(party == ALICE?nullptr:"127.0.0.1", port);
	test<HighSpeedNetIO>(hsnetio);
	delete hsnetio;
}
