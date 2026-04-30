// io/net_io_channel.h — two-socket full-duplex TCP IOChannel.
//
// What's in net_io_channel.h:
//   NetIO(addr, port)                  open ports (port, port+1)
//   NetIO(addr, send_port, recv_port)  explicit dual-port (firewalled)
//   send_data / recv_data              raw bytes (size_t-correct)
//   send_block / recv_block            block-typed wrapper
//   send_bool / recv_bool              packed via utils/bools_to_bits
//   flush()                            drain outbound only (no peer coupling)
//   sync()                             1-byte ping/pong handshake
//
// This test runs as two cooperating processes via ./run. Section ordering
// follows the project convention: correctness first (bytes then bools),
// then a loopback throughput sweep over block sizes.

#include <cassert>
#include <cstring>
#include <iostream>

#include "emp-tool/emp-tool.h"

using namespace std;
using namespace emp;

// -------------------------------------------------------------------------
// run_correctness(): byte stream round-trip at unaligned offsets, then bool
// packing round-trip at unaligned bool offsets. Each side asserts on the
// values it receives.
// -------------------------------------------------------------------------
static void run_correctness(NetIO *io, int party) {
	// Stream of unaligned-byte sends: sends `length` bytes 1000 times in
	// each direction, with `length` chosen to straddle the 32 KiB sender
	// staging buffer (NETWORK_BUFFER_SIZE2/5 + 100) so most send_data calls
	// trigger a staging-overflow path.
	{
		int length = NETWORK_BUFFER_SIZE2 / 5 + 100;
		char *data = new char[length];
		char *data2 = new char[length];
		PRG prg(&zero_block);
		for (int i = 0; i < 1000; ++i) {
			if (party == ALICE) {
				prg.random_data_unaligned(data, length);
				io->send_data(data, length);
				io->send_data(data, length);
				io->recv_data(data2, length);
				assert(memcmp(data, data2, length) == 0);
			} else {
				prg.random_data_unaligned(data2, length);
				io->recv_data(data, length);
				io->recv_data(data, length);
				io->send_data(data2, length);
				assert(memcmp(data, data2, length) == 0);
			}
		}
		io->flush();
		delete[] data;
		delete[] data2;
	}

	// Bool packing: 1 MiB of bools sent both aligned and at offset +7 (so
	// the implementation cannot lean on uint64_t-aligned input).
	{
		PRG prg(&zero_block);
		bool *data  = new bool[1024 * 1024];
		bool *data2 = new bool[1024 * 1024];
		prg.random_bool(data, 1024 * 1024);
		if (party == ALICE) {
			io->send_bool(data, 1024 * 1024);
			io->send_bool(data + 7, 1024 * 1024 - 7);
		} else {
			io->recv_bool(data2, 1024 * 1024);
			assert(memcmp(data2, data, 1024 * 1024) == 0);
			memset(data2, 0, 1024 * 1024);
			io->recv_bool(data2 + 7, 1024 * 1024 - 7);
			assert(memcmp(data2 + 7, data + 7, 1024 * 1024 - 7) == 0);
		}
		delete[] data;
		delete[] data2;
	}

	if (party == ALICE) cout << "correctness: OK\n";
}

// -------------------------------------------------------------------------
// bench(): loopback throughput sweep. Sender measures Gbps; receiver only
// drains. The sweep starts below the staging size to expose small-message
// per-call overhead and runs out to multi-MiB to show bulk steady state.
// -------------------------------------------------------------------------
static void bench(NetIO *io, int party) {
	for (long long length = 2; length <= 8192 * 16; length *= 2) {
		long long times = 1024 * 1024 * 128 / length;
		block *data = new block[length];
		if (party == ALICE) {
			auto start = clock_start();
			for (long long i = 0; i < times; ++i)
				io->send_block(data, length);
			double interval = time_from(start);
			cout << "Loopback speed with block size " << length << " :\t"
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
	parse_party_and_port(argv, &party, &port);
	NetIO *io = new NetIO(party == ALICE ? nullptr : "127.0.0.1", port);

	run_correctness(io, party);
	bench(io, party);

	delete io;
}
