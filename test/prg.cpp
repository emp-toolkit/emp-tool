#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;
using namespace emp;

int main() {
	PRG prg;//using a random seed

	int rand_int;
	prg.random_data(&rand_int, sizeof(rand_int));

	block rand_block[3];
	prg.random_block(rand_block, 3);
		
	prg.reseed(&rand_block[1]);//reset the PRG with another seed

	int rand_ints[100];
	prg.random_data_unaligned(rand_ints+2, sizeof(int)*98);//when the array is not 128-bit-aligned

	mpz_t integ;
	mpz_init(integ);
	prg.random_mpz(integ, 1024);//random number with 1024 bits.

	for (long long length = 2; length <= 8192; length*=2) {
		long long times = 1024*1024*32/length;
		block * data = new block[length];
		auto start = clock_start();
		for (int i = 0; i < times; ++i) {
			prg.random_block(data, length);
		}
		double interval = time_from(start);
		delete[] data;
		cout << "PRG speed with block size "<<length<<" :\t"<<(length*times*128)/(interval+0.0)*1e6*1e-9<<" Gbps\n";
	}
	
	mpz_clear(integ);
	return 0;
}
