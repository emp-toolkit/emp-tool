// io/net_io_channel.h — TCP IOChannel.
//
// Public surface:
//   ctor(addr, port)                   open one TCP fd
//   ctor(existing_sock)                wrap an already-connected fd
//   send_data / recv_data              raw bytes (int64_t lengths)
//   send_block / recv_block            block-typed wrapper
//   send_bool / recv_bool              packed via bools_to_bits
//   flush()                            drain outbound only (no peer coupling)
//
// Benchmark below runs the loopback throughput sweep only. Correctness and
// regression coverage lives in test/test_netio.cpp.

#include <cassert>
#include <cstring>
#include <iostream>

#include "emp-tool/emp-tool.h"

using namespace std;
using namespace emp;

// -------------------------------------------------------------------------
// bench(): loopback throughput sweep. Sender measures Gbps; receiver only
// drains. The sweep starts below the staging size to expose small-message
// per-call overhead and runs out to multi-MiB to show bulk steady state.
// -------------------------------------------------------------------------
template <typename IO>
static void bench(IO *io, int party, const char *tag) {
	if (party == ALICE) cout << "--- " << tag << " bench ---\n";
	for (long long length = 2; length <= 8192 * 16; length *= 2) {
		long long times = 1024 * 1024 * 128 / length;
		block *data = new block[length];
		if (party == ALICE) {
			auto start = clock_start();
			for (long long i = 0; i < times; ++i)
				io->send_block(data, length);
			double interval = time_from(start);
			cout << tag << " loopback size " << length << " :\t"
			     << (length * times * 128) / (interval + 0.0) * 1e6 * 1e-9
			     << " Gbps\n";
		} else {
			for (long long i = 0; i < times; ++i)
				io->recv_block(data, length);
		}
		delete[] data;
	}
}

int main(int argc, char **argv) {
	int port, party;
	party = parse_party(argv);
	port = peer_port();
	auto io = (party == ALICE) ? NetIO::listen(port, true) : NetIO::connect(peer_ip(), port, true);
	bench(io.get(), party, "NetIO");
}
