#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;
using namespace emp;


#include <iomanip>
#include <string>
#include <map>
#include <random>
#include <cmath>
int main() {
	PRG gen;
	std::normal_distribution<> d{5,2};
	cout << d(gen)<<endl;
	std::map<int, int> hist{};
	for(int n=0; n<10000; ++n) {
		++hist[std::round(d(gen))];
	}
	for(auto p : hist) {
		std::cout << std::setw(2)
			<< p.first << ' ' << std::string(p.second/200, '*') << '\n';
	}



	PRG prg;//using a random seed

	block rand_block[3];
	prg.random_block(rand_block, 3);

	prg.reseed(&rand_block[1]);//reset the PRG with another seed

	int rand_ints[100];
	int a = 0;
	prg.random_data_unaligned(rand_ints+1, sizeof(int)*99);//when the array is not 128-bit-aligned
	cout << a<<"\n";

	prg.reseed(&zero_block);
	for (long long length = 2; length <= 8192; length*=2) {
		long long times = 1024*1024*32/length;
		block * data = new block[length+1];
		char * data2 = (char *)data;
		auto start = clock_start();
		for (int i = 0; i < times; ++i) {
			prg.random_data(data2, length*16);
			//prg.random_data_unaligned(data2+1, length*16);
		}
		double interval = time_from(start);
		delete[] data;
		cout << "PRG speed with block size "<<length<<" :\t"<<(length*times*128)/(interval+0.0)*1e6*1e-9<<" Gbps\n";
	}
	return 0;
}
