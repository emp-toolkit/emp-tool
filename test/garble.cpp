#include <iostream>

#include "emp-tool/emp-tool.h"
using namespace std;
using namespace emp;
using Bit = Bit_T<block>; 
using Integer = Integer_T<block>; 

class AbandonIO: public IOChannel<AbandonIO> { public:
	void send_data_internal(const void * data, int len) {
	}

	void recv_data_internal(void  * data, int len) {
	}
};

int port, party;
template <typename T>
void test(T* netio) {
	Bit* a = new Bit[128];
	Bit* b = new Bit[128];
	Bit* c = new Bit[128];
	
	PRG prg;
//	prg.random_block(a, 128);
//	prg.random_block(b, 128);

	string file = "./emp-tool/circuits/files/bristol_format/AES-non-expanded.txt";
	BristolFormat cf(file.c_str());

	if (party == BOB) {
		emp::backend = new HalfGateEva<T>(netio);
		for (int i = 0; i < 100; ++i)
			cf.compute(c, a, b);
		delete emp::backend;
	} else {
		AbandonIO* aio = new AbandonIO();
		emp::backend = new HalfGateGen<AbandonIO>(aio);

		auto start = clock_start();
		for (int i = 0; i < 100; ++i) {
			cf.compute(c, a, b);
		}
		double interval = time_from(start);
		cout << "Pure AES garbling speed : " << 100 * 6800 / interval << " million gate per second\n";
		delete aio;
		delete emp::backend;

		MemIO* mio = new MemIO();
		emp::backend = new HalfGateGen<MemIO>(mio);

		start = clock_start();
		for (int i = 0; i < 20; ++i) {
			mio->clear();
			for (int j = 0; j < 5; ++j)
				cf.compute(c, a, b);
		}
		interval = time_from(start);
		cout << "AES garbling + Writing to Memory : " << 100 * 6800 / interval << " million gate per second\n";
		delete mio;
		delete emp::backend;

		emp::backend = new HalfGateGen<T>(netio);

		start = clock_start();
		for (int i = 0; i < 100; ++i) {
			cf.compute(c, a, b);
		}
		interval = time_from(start);
		cout << "AES garbling + Loopback Network : " << 100 * 6800 / interval << " million gate per second\n";

		delete emp::backend;
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
