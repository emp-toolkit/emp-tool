#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;
using namespace emp;

int main() {
	PRP prp;//using a public seed by default

	block rand_block[3];
	memset(rand_block, 0, sizeof(block)*3);
	prp.permute_block(rand_block, 3);//applying pi on each block of data
		
	for (long long length = 2; length <= 2048; length*=2) {
		long long times = 1024*1024*32/length;
		block * data = new block[length];
		auto start = clock_start();
		for (int i = 0; i < times; ++i) {
			prp.permute_block(data, length);
		}
		double interval = time_from(start);
		delete[] data;
		cout << "PRP speed with block size "<<length<<" :\t"<<(length*times*128)/(interval+0.0)*1e6*1e-9<<" Gbps\n";
	}
	return 0;
}
