#include <iostream>

#include "emp-tool/emp-tool.h"
using namespace std;
using namespace emp;

class AbandonIO: public IOChannel { public:
	void send_data_internal(const void * /*data*/, size_t /*len*/) override {}
	void recv_data_internal(void * /*data*/, size_t /*len*/) override {}
};

int port, party;
template <typename T>
void test(T* netio) {
	Bit* a = new Bit[128];
	Bit* b = new Bit[128];
	Bit* c = new Bit[128];

	PRG prg;
	prg.random_block(reinterpret_cast<block*>(a), 128);
	prg.random_block(reinterpret_cast<block*>(b), 128);

	string file = "./emp-tool/circuits/files/bristol_format/AES-non-expanded.txt";
	BristolFormat cf(file.c_str());

	const int N = 1000;
	if (party == BOB) {
		backend = new HalfGateEva(netio);
		for (int i = 0; i < N; ++i)
			cf.compute(c, a, b);
		delete backend;
		backend = nullptr;
	} else {
		AbandonIO* aio = new AbandonIO();
		backend = new HalfGateGen(aio);

		auto start = clock_start();
		for (int i = 0; i < N; ++i) {
			cf.compute(c, a, b);
		}
		double interval = time_from(start);
		cout << "Pure AES garbling speed : " << N * 6800 / interval << " million gate per second\n";
		delete aio;
		delete backend; backend = nullptr;

		MemIO* mio = new MemIO();
		backend = new HalfGateGen(mio);

		start = clock_start();
		for (int i = 0; i < N/5; ++i) {
			mio->clear();
			for (int j = 0; j < 5; ++j)
				cf.compute(c, a, b);
		}
		interval = time_from(start);
		cout << "AES garbling + Writing to Memory : " << N * 6800 / interval << " million gate per second\n";
		delete mio;
		delete backend; backend = nullptr;

		backend = new HalfGateGen(netio);

		start = clock_start();
		for (int i = 0; i < N; ++i) {
			cf.compute(c, a, b);
		}
		interval = time_from(start);
		cout << "AES garbling + Loopback Network : " << N * 6800 / interval << " million gate per second\n";

		delete backend; backend = nullptr;
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
}
