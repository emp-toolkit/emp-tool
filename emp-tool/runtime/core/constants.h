#ifndef EMP_CONFIG_H__
#define EMP_CONFIG_H__
#include <cstddef>
#include <cstdint>
namespace emp {
// Counter-mode AES chunk size in blocks: bounds the PRG refill working set
// and amortizes drain_tiles' per-call ramp. Any multiple of the widest
// ParaEnc tile drains through the same instruction stream, and throughput is
// flat across a wide range of sizes, so the value is a footprint choice, not
// a per-machine tunable.
constexpr int64_t AES_BATCH_SIZE = 64;
// NetIO staging: send-side coalescing buffer and recv-side read() target.
// Sized so bulk transfers don't syscall too often while keeping the two
// per-channel buffers small; per-flow TCP throughput is governed by the
// kernel socket buffers, not by this. Enlarging it gains no throughput and
// only makes consumption burstier, which stalls the peer's pipeline.
const static int NETWORK_STAGING_BUFFER_SIZE = 1024*32;
const static int NETWORK_STREAM_BUFFER_SIZE = 1024*1024;

// Bool-packing chunk size in bools for the bulk bool send/recv path.
// Packs 8 bools per byte, so the on-stack staging is IO_BOOL_CHUNK_SIZE/8 bytes.
const static size_t IO_BOOL_CHUNK_SIZE = 8 * 1024;

// Party tags — the ONE definition of the owner/recipient vocabulary used by
// every input(owner, ...) / reveal(value, recipient) across the stack; the
// session contracts (ir/session/) reference this domain rather than redefining
// it. Unscoped + : int so `int party` parameters accept these implicitly.
//
//   owner / recipient >= 1   a concrete party. ALICE / BOB name parties 1 and 2
//                            for the two-party protocols; n-party protocols use
//                            plain ids 1..n with the same meaning.
//   PUBLIC (= 0)             every party: a public input / reveal-to-all.
//   XOR    (= -1)            reveal-recipient sentinel only: "leave as
//                            XOR-shares, reveal to no one". Only sessions that
//                            document XOR-share reveal support it (e.g.
//                            SH2PCSession); all others reject it loudly.
enum Party : int { XOR = -1, PUBLIC = 0, ALICE = 1, BOB = 2 };

// Upper bound on a group point's serialized length on the wire.
// EC points are ≤ ~129 bytes; this caps wire-corruption at recv.
const static uint32_t MAX_POINT_BYTES = 2048;

}
#endif// CONFIG_H__
