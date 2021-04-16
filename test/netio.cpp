#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;
using namespace emp;

int port, party;
template<typename T>
void test(T * io) {
	if(party == ALICE) {
		for (long long length = 2; length <= 8192*16; length*=2) {
			long long times = 1024*1024*128/length;
			block * data = new block[length];
			auto start = clock_start();
			for (int i = 0; i < times; ++i) {
				io->send_block(data, length);
			}
			double interval = time_from(start);
			delete[] data;
			cout << "Loopback speed with block size "<<length<<" :\t"<<(length*times*128)/(interval+0.0)*1e6*1e-9<<" Gbps\n";
		}
	} else {//party == BOB
		for (long long length = 2; length <= 8192*16; length*=2) {
			long long times = 1024*1024*128/length;
			block * data = new block[length];
			for (int i = 0; i < times; ++i) {
				io->recv_block(data, length);
			}
			delete[] data;
		}
	}
	PRG prg(&zero_block);
	bool * data = new bool[1024*1024];
	bool * data2 = new bool[1024*1024];
	prg.random_bool(data, 1024*1024);
	if(party == ALICE) {
		io->send_bool(data, 1024*1024);
		io->send_bool(data+7, 1024*1024-7);
	} else {
		io->recv_bool(data2, 1024*1024);
		assert(memcmp(data2, data, 1024*1024) == 0);
		memset(data2, 0, 1024*1024);
		io->recv_bool(data2+7, 1024*1024-7);
		assert(memcmp(data2+7, data+7, 1024*1024-7) == 0);
	}
	delete[] data;
	delete[] data2;
}
int main(int argc, char** argv) {
	parse_party_and_port(argv, &party, &port);

	cout <<"NetIO\n";
	NetIO * io = new NetIO(party == ALICE ? nullptr:"127.0.0.1", port);
	test<NetIO>(io);
	delete io;

	cout <<"HighSpeed NetIO\n";
	HighSpeedNetIO * hsio = new HighSpeedNetIO(party == ALICE ? nullptr:"127.0.0.1", port, port+1);
	test<HighSpeedNetIO>(hsio);
	delete hsio;
}
