#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;
using namespace emp;

int main() {
	Commitment c;

	char * data = new char[1024*1024];
	Com com;
	Decom decom;
	c.commit(decom, com, data, 1024*1024);
	if(!c.open(decom, com, data, 1024*1024)) {
		error("Commitment: open failed!");
	}
	
	delete[] data;
	for (long long length = 2; length <= 2048; length*=2) {
		long long times = 1024*1024*32/length;
		block * data = new block[length];
		auto start = clock_start();
		for (int i = 0; i < times; ++i) {
			c.commit(decom, com, data, length*sizeof(block));
			c.open(decom, com, data, length*sizeof(block));
		}
		double interval = time_from(start);
		delete[] data;
		cout << "Commit+Open speed with block size "<<length<<" :\t"<<(length*times*128)/(interval+0.0)*1e6*1e-9<<" Gbps\n";
	}
	return 0;
}
