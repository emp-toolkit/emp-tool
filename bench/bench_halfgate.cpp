// Half-gate garble/eval throughput. Build-only (not a ctest case); the
// correctness check lives in ../test/test_halfgate.cpp.
#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;
using namespace emp;

int main(void) {
	constexpr int64_t gates = 2 * 1024 * 1024;
	block data[2], delta, table[2], w0, w1;
	MITCCRH<8> mi_gen;
	PRG prg;
	prg.random_block(&delta, 1);
	delta = delta | makeBlock(0x0, 0x1);
	mi_gen.setS(delta);

	block data1[2];
	MITCCRH<8> mi_eva;
	mi_eva.setS(delta);
	block ret;

	cout << "Efficiency: ";
	auto start = clock_start();
	for(int64_t i = 0; i < gates; ++i) {
		prg.random_block(data, 2);
		w0 = halfgates_garble(data[0], data[0]^delta, data[1], data[1]^delta, delta, table, &mi_gen);
		w1 = w0 ^ delta;

		data1[0] = data[0] ^ delta;
		data1[1] = data[1] ^ delta;
		ret = halfgates_eval(data1[0], data1[1], table, &mi_eva);
	}
	asm volatile("" : "+m"(*(char *)&w1), "+m"(*(char *)&ret) : : "memory");
	cout << gates / time_from(start) * 1e6 << " gates/second" << endl;

	return 0;
}
