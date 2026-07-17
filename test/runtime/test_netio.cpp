// io/net_io_channel.h — TCP IOChannel.
//
// Public surface:
//   ctor(addr, port)                   open one TCP fd
//   ctor(existing_sock)                wrap an already-connected fd
//   send_data / recv_data              raw bytes (int64_t lengths)
//   send_block / recv_block            block-typed wrapper
//   send_bool / recv_bool              packed via bools_to_bits
//   flush()                            drain outbound only (no peer coupling)
//   sync()                             1-byte ping/pong handshake
//   make_sibling()                     more connections on the same port
//
// Test functions below are templated on the IO type so correctness and
// regression checks can be reused by IO implementations.

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
				expecting(memcmp(data, data2, length) == 0,
				          "NetIO test: ALICE byte round-trip mismatch");
			} else {
				prg.random_data_unaligned(data2, length);
				io->recv_data(data, length);
				io->recv_data(data, length);
				io->send_data(data2, length);
				expecting(memcmp(data, data2, length) == 0,
				          "NetIO test: BOB byte round-trip mismatch");
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
			expecting(memcmp(data2, data, 1024 * 1024) == 0,
			          "NetIO test: aligned bool round-trip mismatch");
			memset(data2, 0, 1024 * 1024);
			io->recv_bool(data2 + 7, 1024 * 1024 - 7);
			expecting(memcmp(data2 + 7, data + 7, 1024 * 1024 - 7) == 0,
			          "NetIO test: unaligned bool round-trip mismatch");
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
		IO io(party == ALICE ? nullptr : peer_ip(), port + 1, true);
		if (party == ALICE) {
			io.send_data(data, N);
			io.flush();                // peer's recv depends on this
			char ack = 0;
			io.recv_data(&ack, 1);     // hold connection open until BOB confirms
			expecting(ack == 1, "NetIO test: explicit-flush acknowledgement mismatch");
		} else {
			io.recv_data(data2, N);
			expecting(memcmp(data, data2, N) == 0,
			          "NetIO test: explicit-flush payload mismatch");
			char ack = 1;
			io.send_data(&ack, 1);
			io.flush();
		}
	}

	// (b) destructor drain. ALICE sends N then immediately destroys the
	// IO — no flush(), no recv. BOB must still see N. ~IO is the only
	// thing that can move the bytes.
	{
		IO *io = new IO(party == ALICE ? nullptr : peer_ip(), port + 2, true);
		if (party == ALICE) {
			io->send_data(data, N);
			delete io;                 // must flush; otherwise BOB hits EOF
		} else {
			io->recv_data(data2, N);
			expecting(memcmp(data, data2, N) == 0,
			          "NetIO test: destructor-flush payload mismatch");
			delete io;
		}
	}

	delete[] data;
	delete[] data2;
	if (party == ALICE) cout << tag << " send-only regression: OK\n";
}

// Create many live siblings on the same port. Half are created from the primary;
// then the primary is destroyed and the rest are created from the first sibling.
// This verifies shared listener ownership, repeated accept/connect pairing, and
// closure only after the last related NetIO dies. A fresh session then reuses the
// same port.
static void run_sibling_regression(int port, int party) {
	{
		auto primary = party == ALICE ? NetIO::listen(port, true)
		                              : NetIO::connect(peer_ip(), port, true);
		std::vector<std::unique_ptr<NetIO>> siblings;
		siblings.reserve(16);
		for (int round = 0; round < 16; ++round) {
			NetIO *source = round < 8 ? primary.get() : siblings.front().get();
			siblings.push_back(source->make_sibling());
			siblings.back()->sync();
			if (round == 7) primary.reset();
		}
	}
	{
		auto primary = party == ALICE ? NetIO::listen(port, true)
		                              : NetIO::connect(peer_ip(), port, true);
		auto sibling = primary->make_sibling();
		sibling->sync();
	}
	if (party == ALICE) cout << "NetIO shared-listener regression: OK\n";
}

// Run the full correctness/regression suite on one IO type,
// using a contiguous block of three ports starting at port_base:
// port_base+0 = main channel, +1 / +2 = regression channels.
template <typename IO>
static void run_suite(int port_base, int party, const char *tag) {
	IO *io = new IO(party == ALICE ? nullptr : peer_ip(), port_base, true);
	run_correctness(io, party, tag);
	run_send_only_regression<IO>(port_base, party, tag);
	delete io;
}

int main(int argc, char **argv) {
	int port, party;
	party = parse_party(argv);
	port = peer_port();

	// Four contiguous ports: main, two send-only cases, sibling regression.
	run_suite<NetIO>(port, party, "NetIO");
	run_sibling_regression(port + 3, party);
}
