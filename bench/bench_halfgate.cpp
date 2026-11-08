// Half-gate garble/eval throughput. Build-only (not a ctest case); the
// correctness check lives in test/runtime/test_halfgate.cpp.
//
// Every output of the loop is folded into `sink` and printed: with the hashes
// inlined into main and their results otherwise unused, the compiler is free
// to delete them, and then the loop times only the PRG and the key schedules.
#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;
using namespace emp;

int main(void) {
	block data[2], delta, hash_seed, table[2], w0, w1;
	MITCCRH<8> mi_gen;
	PRG prg;
	prg.random_block(&delta, 1);
	delta = delta | makeBlock(0x0, 0x1);
	prg.random_block(&hash_seed, 1);
	mi_gen.setS(hash_seed);

	block data1[2];
	MITCCRH<8> mi_eva;
	mi_eva.setS(hash_seed);
	block ret;
	block sink = zero_block;

	const int64_t gates = 1024 * 1024 * 2;
	cout << "Efficiency: ";
	auto start = clock_start();
	for(int64_t i = 0; i < gates; ++i) {
		prg.random_block(data, 2);
		w0 = halfgates_garble(data[0], data[0]^delta, data[1], data[1]^delta, delta, table, &mi_gen);
		w1 = w0 ^ delta;

		data1[0] = data[0] ^ delta;
		data1[1] = data[1] ^ delta;
		ret = halfgates_eval(data1[0], data1[1], table, &mi_eva);
		sink = sink ^ ret ^ w1 ^ table[0] ^ table[1];
	}
	double us = time_from(start);
	cout << (double)gates / us * 1e6 << " gates/second (sink " << sink << ")" << endl;

	return 0;
}
