#include "emp-tool/emp-tool.h"
#include <array>
#include <iostream>
using namespace std;
using namespace emp;

void printt(block a) {
	unsigned char *c = (unsigned char*)(&a);
	for(int i = 0; i < 16; ++i) printf("%x ", c[i]);
	printf("\n");
}

template <int BatchSize>
static void garble_trace(const array<array<block, 2>, 12>& labels,
                         block delta, block hash_seed,
                         array<block, 24>& tables,
                         array<block, 12>& outputs) {
	MITCCRH<BatchSize> hash;
	hash.setS(hash_seed);
	for (size_t i = 0; i < labels.size(); ++i) {
		outputs[i] = halfgates_garble(
		    labels[i][0], labels[i][0] ^ delta,
		    labels[i][1], labels[i][1] ^ delta,
		    delta, tables.data() + 2 * i, &hash);
	}
}

static bool check_batch_size_invariance(PRG& prg, block delta,
                                        block hash_seed) {
	array<array<block, 2>, 12> labels;
	for (auto& pair : labels) prg.random_block(pair.data(), pair.size());
	array<block, 24> tables2, tables8;
	array<block, 12> outputs2, outputs8;
	garble_trace<2>(labels, delta, hash_seed, tables2, outputs2);
	garble_trace<8>(labels, delta, hash_seed, tables8, outputs8);
	return cmpBlock(tables2.data(), tables8.data(), tables2.size()) &&
	       cmpBlock(outputs2.data(), outputs8.data(), outputs2.size());
}

int main(void) {
	// sender
	block data[2], delta, hash_seed, table[2], w0, w1;
	MITCCRH<8> mi_gen;
	PRG prg;
	prg.random_block(&delta, 1);
	delta = delta | makeBlock(0x0, 0x1);
	prg.random_block(&hash_seed, 1);
	mi_gen.setS(hash_seed);

	// receiver
	block data1[2];
	MITCCRH<8> mi_eva;
	mi_eva.setS(hash_seed);
	block ret;


	cout << "Correctness ... ";
	for(int ii = 0; ii < 2; ++ii) {
		for(int jj = 0; jj < 2; ++jj) {
			for(int i = 0; i < 8; ++i) {
				prg.random_block(data, 2);
				w0 = halfgates_garble(data[0], data[0]^delta, data[1], data[1]^delta, delta, table, &mi_gen);
				w1 = w0 ^ delta;

				if(ii == 1) data1[0] = data[0] ^ delta; else data1[0] = data[0];
				if(jj == 1) data1[1] = data[1] ^ delta; else data1[1] = data[1];
				ret = halfgates_eval(data1[0], data1[1], table, &mi_eva);

				block ret1 = w0;
				if(ii == 1 && jj == 1) ret1 = w1;
				if(cmpBlock(&ret, &ret1, 1) == false) {cout << "wrong" << endl; abort();}
			}
		}
	}
	cout << "check\n";
	bool batch_invariant = check_batch_size_invariance(prg, delta, hash_seed);
	cout << "BatchSize-invariant transcript ... "
	     << (batch_invariant ? "check" : "FAIL") << "\n";
	// Throughput measurement lives in ../bench/bench_halfgate.cpp (build-only).

	return batch_invariant ? 0 : 1;
}
