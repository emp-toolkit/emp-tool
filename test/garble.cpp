#include <iostream>

#include "emp-tool/emp-tool.h"
using namespace std;
using namespace emp;

class AbandonIO: public IOChannel<AbandonIO> { public:
	void send_data_internal(const void * data, int len) {
	}

	void recv_data_internal(void  * data, int len) {
	}
};

int port, party;
template <typename T>
void test(T* netio) {
	block* a = new block[128];
	block* b = new block[128];
	block* c = new block[128];

	PRG prg;
	prg.random_block(a, 128);
	prg.random_block(b, 128);

	string file = "./emp-tool/circuits/files/bristol_format/AES-non-expanded.txt";
	BristolFormat cf(file.c_str());

	if (party == BOB) {
		HalfGateEva<T>::circ_exec = new HalfGateEva<T>(netio);
		for (int i = 0; i < 100; ++i)
			cf.compute(c, a, b);
		delete HalfGateEva<T>::circ_exec;
	} else {
		AbandonIO* aio = new AbandonIO();
		HalfGateGen<AbandonIO>::circ_exec = new HalfGateGen<AbandonIO>(aio);

		auto start = clock_start();
		for (int i = 0; i < 100; ++i) {
			cf.compute(c, a, b);
		}
		double interval = time_from(start);
		cout << "Pure AES garbling speed : " << 100 * 6800 / interval << " million gate per second\n";
		delete aio;
		delete HalfGateGen<AbandonIO>::circ_exec;

		MemIO* mio = new MemIO();
		HalfGateGen<MemIO>::circ_exec = new HalfGateGen<MemIO>(mio);

		start = clock_start();
		for (int i = 0; i < 20; ++i) {
			mio->clear();
			for (int j = 0; j < 5; ++j)
				cf.compute(c, a, b);
		}
		interval = time_from(start);
		cout << "AES garbling + Writing to Memory : " << 100 * 6800 / interval << " million gate per second\n";
		delete mio;
		delete HalfGateGen<MemIO>::circ_exec;

		HalfGateGen<T>::circ_exec = new HalfGateGen<T>(netio);

		start = clock_start();
		for (int i = 0; i < 100; ++i) {
			cf.compute(c, a, b);
		}
		interval = time_from(start);
		cout << "AES garbling + Loopback Network : " << 100 * 6800 / interval << " million gate per second\n";

		delete HalfGateGen<T>::circ_exec;
	}

	delete[] a;
	delete[] b;
	delete[] c;
}

int main(int argc, char** argv) {
	parse_party_and_port(argv, &party, &port);
	cout << "Using NetIO\n";
	NetIO* netio = new NetIO(party == ALICE ? nullptr : "127.0.0.1", port);
	test<NetIO>(netio);
	delete netio;

	cout << "Using HighSpeedNetIO\n";
	HighSpeedNetIO* hsnetio = new HighSpeedNetIO(party == ALICE ? nullptr : "127.0.0.1", port, port+1);
	test<HighSpeedNetIO>(hsnetio);
	delete hsnetio;
}
