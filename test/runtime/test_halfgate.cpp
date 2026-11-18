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

template <class Hash>
static void garble_trace(const array<array<block, 2>, 12>& labels,
                         block delta, block hash_seed,
                         array<block, 24>& tables,
                         array<block, 12>& outputs) {
	Hash hash;
	hash.setS(hash_seed);
	for (size_t i = 0; i < labels.size(); ++i) {
		outputs[i] = halfgates_garble(
		    labels[i][0], labels[i][0] ^ delta,
		    labels[i][1], labels[i][1] ^ delta,
		    delta, tables.data() + 2 * i, &hash);
	}
}

// The same 12 gates garbled under two hash instances must give the same tables
// and output labels.
template <class HashA, class HashB>
static bool same_transcript(PRG& prg, block delta, block hash_seed) {
	array<array<block, 2>, 12> labels;
	for (auto& pair : labels) prg.random_block(pair.data(), pair.size());
	array<block, 24> tables_a, tables_b;
	array<block, 12> outputs_a, outputs_b;
	garble_trace<HashA>(labels, delta, hash_seed, tables_a, outputs_a);
	garble_trace<HashB>(labels, delta, hash_seed, tables_b, outputs_b);
	return cmpBlock(tables_a.data(), tables_b.data(), tables_a.size()) &&
	       cmpBlock(outputs_a.data(), outputs_b.data(), outputs_a.size());
}

int main(void) {
	// sender
	block data[2], delta, hash_seed, table[2], w0, w1;
	MITCCRH<> mi_gen;
	PRG prg;
	prg.random_block(&delta, 1);
	delta = delta | makeBlock(0x0, 0x1);
	prg.random_block(&hash_seed, 1);
	mi_gen.setS(hash_seed);

	// receiver
	block data1[2];
	MITCCRH<> mi_eva;
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

	// Key-reuse regression (emp-tool issue #212). MITCCRH<> keys AES per
	// 8-gid bucket and a half-gate spends gids 2g, 2g+1, so four consecutive
	// ANDs share one key. Garble AND(a, b) then AND(a, c) in one bucket: if the
	// hash ignored the gid, the first rows would be H(A0)^H(A1)^pb*delta and
	// H(A0)^H(A1)^pc*delta, their XOR 0 or delta, and the evaluator would read
	// delta off the tables. AND(a, a) is the one-gate version:
	// table[0]^table[1]^A0 would be pa*delta. With the gid in the cipher input
	// both differences are unstructured.
	cout << "Bucket key reuse: fan-out rows unstructured ... ";
	for (int trial = 0; trial < 16; ++trial) {
		MITCCRH<> mi;
		block S, a, b, c, t_ab[2], t_ac[2], t_aa[2];
		prg.random_block(&S, 1);
		mi.setS(S);
		prg.random_block(&a, 1);
		prg.random_block(&b, 1);
		prg.random_block(&c, 1);
		halfgates_garble(a, a ^ delta, b, b ^ delta, delta, t_ab, &mi);
		halfgates_garble(a, a ^ delta, c, c ^ delta, delta, t_ac, &mi);
		halfgates_garble(a, a ^ delta, a, a ^ delta, delta, t_aa, &mi);
		block fan_out = t_ab[0] ^ t_ac[0];
		block self    = t_aa[0] ^ t_aa[1] ^ a;
		if (cmpBlock(&fan_out, &zero_block, 1) || cmpBlock(&fan_out, &delta, 1) ||
		    cmpBlock(&self, &zero_block, 1) || cmpBlock(&self, &delta, 1)) {
			cout << "LEAK: bucket-shared key without gid tweak\n";
			abort();
		}
	}
	cout << "check\n";
	// Ahead only decides when keys are scheduled, and the stream hashes each
	// gid as MITCCRHBulk does: Ahead 2 and Ahead 8 give one transcript, and so
	// do the two classes.
	bool ahead_invariant = same_transcript<MITCCRH<3, 2>, MITCCRH<3, 8>>(prg, delta, hash_seed);
	cout << "Ahead-invariant transcript ... "
	     << (ahead_invariant ? "check" : "FAIL") << "\n";
	bool bulk_equal = same_transcript<MITCCRH<>, MITCCRHBulk<8>>(prg, delta, hash_seed);
	cout << "MITCCRH and MITCCRHBulk transcripts equal ... "
	     << (bulk_equal ? "check" : "FAIL") << "\n";
	// Throughput measurement lives in bench/bench_halfgate.cpp (build-only).

	return ahead_invariant && bulk_equal ? 0 : 1;
}
