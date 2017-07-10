#include "prg.h"
#include "utils.h"
#include <iostream>
using namespace std;

int main() {
	PRG prg;//using a random seed
	int rand_int;
	prg.random_data(&rand_int, sizeof(rand_int));

	block rand_block[3];
	prg.random_block(rand_block, 3);

	int rand_ints[100];
	prg.random_data_unaligned(rand_ints+2, 100);

	initialize_relic();
	bn_t bn1, bn2, bn3;
	bn_newl(bn1, bn2, bn3);
	prg.random_bn(bn1, bn2, bn3);

	eb_t eb1, eb2, eb3;
	eb_newl(eb1, eb2, eb3);
	prg.random_eb(eb1, eb2, eb3);

	mpz_t integ;
	mpz_init(integ);
	prg.random_mpz(integ, 1024);//random number with 1024 bits.

	for (long long length = 2; length <= 2048; length*=4) {
		long long times = 1024*1024*1024/length;
		block * data = new block[length];
		int64_t t1 = timeStamp();
		for (int i = 0; i < times; ++i) {
			prg.random_block(data, length);
		}
		int64_t t2 = timeStamp();
		delete data;
		cout << "PRG speed with block size "<<length<<" :\t"<<(length*times*128)/(t2-t1+0.0)/1e3<<" Gbps\n";
	}
	return 0;
}
