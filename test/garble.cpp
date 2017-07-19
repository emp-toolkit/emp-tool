#include "gc/halfgate_gen.h"
#include "gc/halfgate_eva.h"
#include "circuits/circuit_file.h"
#include "io/abandon_io_channel.h"
#include "io/mem_io_channel.h"
#include "utils/utils.h"
#include <iostream>
using namespace std;

int port, party;
template<RTCktOpt rt>
void test(NetIO * netio) {
	block *a = new block[128];
	block *b = new block[128];
	block *c = new block[128];

	PRG prg;
	prg.random_block(a, 128);
	prg.random_block(b, 128);

	string file = "circuits/files/AES-non-expanded.txt";
	CircuitFile cf(file.c_str());

	if(party == BOB) {
		local_gc = new HalfGateEva<NetIO, rt>(netio);
		for(int i = 0; i < 10000; ++i)
			cf.compute(c, a, b);
		delete local_gc;
	} else {
		AbandonIO * aio = new AbandonIO();
		local_gc = new HalfGateGen<AbandonIO, rt>(aio);

		auto start = clock_start();
		for(int i = 0; i < 10000; ++i) {
			cf.compute(c, a, b);
		}
		double interval = time_from(start);
		cout << "Pure AES garbling speed : "<< 10000*6800/interval<<" million gate per second\n";
		delete aio;
		delete local_gc;

		MemIO * mio = new MemIO(cf.table_size()*100);
		local_gc = new HalfGateGen<MemIO, rt>(mio);

		start = clock_start();
		for(int i = 0; i < 100; ++i) {
			mio->clear();
			for(int j = 0; j < 100; ++j)
				cf.compute(c, a, b);
		}
		interval = time_from(start);
		cout << "AES garbling + Writing to Memory : "<< 10000*6800/interval<<" million gate per second\n";
		delete mio;
		delete local_gc;

		local_gc = new HalfGateGen<NetIO, rt>(netio);

		start = clock_start();
		for(int i = 0; i < 10000; ++i) {
			cf.compute(c, a, b);
		}
		interval = time_from(start);
		cout << "AES garbling + Loopback Network : "<< 10000*6800/interval<<" million gate per second\n";

		delete local_gc;
	}

}
int main(int argc, char** argv) {
	parse_party_and_port(argv, argc, &party, &port);
	NetIO* netio = new NetIO(party == ALICE?nullptr:"127.0.0.1", 54213);
	test<RTCktOpt::on>(netio);
	test<RTCktOpt::off>(netio);
	delete netio;
}
