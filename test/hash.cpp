#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;
using namespace emp;

int main() {
	Hash hash;

	char * data = new char[1024*1024];
	hash.put(data, 1024*1024);
	char dig[Hash::DIGEST_SIZE];
	hash.digest(dig);
	delete[] data;	

	PRG prg;
	for (long long length = 2; length <= 8192; length*=2) {
		long long times = 1024*1024*32/length;
		block * data = new block[length];
		prg.random_block(data, length);
		auto start = clock_start();
		for (int i = 0; i < times; ++i) {
			hash.put(data, length*sizeof(block));
			hash.digest(dig);
		}
		double interval = time_from(start);
		delete[] data;
		cout << "Hash speed with block size "<<length<<" :\t"<<(length*times*128)/(interval+0.0)*1e6*1e-9<<" Gbps\n";
	}
	return 0;
}
