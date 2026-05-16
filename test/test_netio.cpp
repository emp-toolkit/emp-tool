// io/net_io_channel.h — TCP IOChannel.
//
// Public surface:
//   ctor(addr, port)                   open one TCP fd
//   ctor(existing_sock)                wrap an already-connected fd
//   send_data / recv_data              raw bytes (size_t-correct)
//   send_block / recv_block            block-typed wrapper
//   send_bool / recv_bool              packed via bools_to_bits
//   flush()                            drain outbound only (no peer coupling)
//   sync()                             1-byte ping/pong handshake
//
// Test functions below are templated on the IO type so the same
// correctness + regression + bench checks run via run_suite.

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
template <typename IO>
static void run_correctness(IO *io, int party, const char *tag) {
	// Stream of unaligned-byte sends: sends `length` bytes 1000 times in
	// each direction, with `length` chosen to straddle the 32 KiB sender
	// staging buffer (NETWORK_STAGING_BUFFER_SIZE/5 + 100) so most send_data calls
	// trigger a staging-overflow path.
	{
		int length = NETWORK_STAGING_BUFFER_SIZE / 5 + 100;
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
	// ALICE's send_bool batches push ~256 KiB into stdio's 1 MiB stream_buf
	// without any follow-up recv to trigger an auto-flush; BOB's recv_bool
	// blocks until those bytes are on the wire. An explicit flush here keeps
	// the section closed under composition — without it, if anything between
	// run_correctness and the next high-rate sender is removed, the stranded
	// bytes only escape via the stream_buf overflow path (i.e. by accident).
	io->flush();

	if (party == ALICE) cout << tag << " correctness: OK\n";
}

// -------------------------------------------------------------------------
// run_send_only_regression(): an IO that does only sends across an entire
// protocol step (no recv on the same IO to trigger an auto-flush, no
// volume large enough to overflow the 32 KiB user-space staging buffer
// or the 1 MiB stdio stream buffer) must still deliver its tail bytes to
// the peer. Mirrors the IKNP receiver-role pattern where setup_recv ends
// with ~4 KiB of OTCO::send writes and rcot_recv_end ends with check_x +
// check_t — both ranges are below NETWORK_STAGING_BUFFER_SIZE (32 KiB), so the
// only things that can move the bytes are an explicit flush() or ~IO.
// Two checks, on two short-lived IOs that don't pollute the main channel:
//   (a) explicit io.flush() drains a send-only batch.
//   (b) ~IO drains a send-only batch.
// In a regression where (a)'s drain path is broken, BOB's recv hangs on
// the bytes left in send_buf. In a regression where (b)'s drain path is
// broken, ALICE closes the socket without sending — BOB's recv hits EOF
// and exits with "error: net_recv_data".
// -------------------------------------------------------------------------
template <typename IO>
static void run_send_only_regression(int port, int party, const char *tag) {
	constexpr int N = 4096;            // well under NETWORK_STAGING_BUFFER_SIZE (32 KiB)
	char *data  = new char[N];
	char *data2 = new char[N];
	PRG prg(&zero_block);
	prg.random_data_unaligned(data, N);

	// (a) explicit flush() drains a send-only batch.
	{
		IO io(party == ALICE ? nullptr : "127.0.0.1", port + 1, true);
		if (party == ALICE) {
			io.send_data(data, N);
			io.flush();                // peer's recv depends on this
			char ack = 0;
			io.recv_data(&ack, 1);     // hold connection open until BOB confirms
			assert(ack == 1);
		} else {
			io.recv_data(data2, N);
			assert(memcmp(data, data2, N) == 0);
			char ack = 1;
			io.send_data(&ack, 1);
			io.flush();
		}
	}

	// (b) destructor drain. ALICE sends N then immediately destroys the
	// IO — no flush(), no recv. BOB must still see N. ~IO is the only
	// thing that can move the bytes.
	{
		IO *io = new IO(party == ALICE ? nullptr : "127.0.0.1", port + 2, true);
		if (party == ALICE) {
			io->send_data(data, N);
			delete io;                 // must flush; otherwise BOB hits EOF
		} else {
			io->recv_data(data2, N);
			assert(memcmp(data, data2, N) == 0);
			delete io;
		}
	}

	delete[] data;
	delete[] data2;
	if (party == ALICE) cout << tag << " send-only regression: OK\n";
}

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

// Run the full suite (correctness + regression + bench) on one IO type,
// using a contiguous block of three ports starting at port_base:
// port_base+0 = main channel, +1 / +2 = regression channels.
template <typename IO>
static void run_suite(int port_base, int party, const char *tag) {
	IO *io = new IO(party == ALICE ? nullptr : "127.0.0.1", port_base, true);
	run_correctness(io, party, tag);
	run_send_only_regression<IO>(port_base, party, tag);
	bench(io, party, tag);
	delete io;
}

int main(int argc, char **argv) {
	int port, party;
	parse_party_and_port(argv, &party, &port);

	// Three contiguous ports: main, regression-a, regression-b.
	run_suite<NetIO>(port, party, "NetIO");
}
