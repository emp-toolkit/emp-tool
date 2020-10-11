#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;
using namespace emp;

int main(int argc, char** argv) {
	int port, party;
	parse_party_and_port(argv, &party, &port);
	NetIO * io = new NetIO(party == ALICE ? nullptr:"127.0.0.1", port);

	int length = NETWORK_BUFFER_SIZE2/5+100;
	char * data = new char[length];
	char * data2 = new char[length];
	PRG prg(&zero_block);
	for(int i = 0; i < 1000; ++i)
	if(party == ALICE) {
		prg.random_data(data, length);
		io->send_data(data, length);
		io->send_data(data, length);
//		io->flush();
		io->recv_data(data2, length);
		assert(memcmp(data, data2, length) == 0);
	} else {//party == BOB
		prg.random_data(data2, length);
		io->recv_data(data, length);
		io->recv_data(data, length);
//		io->flush();
		io->send_data(data2, length);
		assert(memcmp(data, data2, length) == 0);
	}
	io->flush();
	cout <<"done\n";
	delete io;
}

