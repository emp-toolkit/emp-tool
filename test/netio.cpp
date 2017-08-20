#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;
using namespace emp;

int main(int argc, char** argv) {
	int port, party;
	parse_party_and_port(argv, 2, &party, &port);
	NetIO * io = new NetIO(party == ALICE ? nullptr:"127.0.0.1", port);

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
	delete io;
}

