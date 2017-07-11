#include "gc/halfgate_gen.h"
#include "circuits/circuit_file.h"
#include "io/mem_io_channel.h"
#include "utils/utils.h"
#include <iostream>
using namespace std;

int main() {
	block *a = new block[128];
	block *b = new block[128];
	block *c = new block[128];
	
	PRG prg;
	prg.random_block(a, 128);
	prg.random_block(b, 128);

	string file = "circuits/files/AES-non-expanded.txt";
	cout << file<<endl;
	CircuitFile cf(file.c_str());
	MemIO * memio = new MemIO(cf.table_size());
	local_gc = new HalfGateGen<MemIO>(memio);

	auto start = clock_start();
	for(int i = 0; i < 10000; ++i) {
		memio->clear();
		cf.compute(c, a, b);
	}
	double interval = time_from(start);
	cout << "AES garbling time: "<< 10000*6800/interval*1e-3<<" million gate per second\n";
}