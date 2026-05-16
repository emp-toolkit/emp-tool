#ifndef EMP_CONFIG_H__
#define EMP_CONFIG_H__
#include <cstddef>
namespace emp {
// Counter-mode AES tile size in blocks. Tuned for L1d locality and to
// amortize ParaEnc's per-call setup (round-key broadcasts).
const static int AES_BATCH_SIZE = 64;
const static int NETWORK_STAGING_BUFFER_SIZE = 1024*32;
const static int NETWORK_STREAM_BUFFER_SIZE = 1024*1024;

// Bool-packing chunk size in bools for the bulk bool send/recv path.
// Packs 8 bools per byte, so the on-stack staging is IO_BOOL_CHUNK_SIZE/8 bytes.
const static size_t IO_BOOL_CHUNK_SIZE = 8 * 1024;

// Party tags. XOR (= -1) is a reveal sentinel meaning "leave as XOR-shares,
// don't reveal to anyone"; PUBLIC (= 0) means "reveal to both"; ALICE / BOB
// are the two parties. Unscoped + : int so `int party` parameters accept
// these values implicitly.
enum Party : int { XOR = -1, PUBLIC = 0, ALICE = 1, BOB = 2 };

// Bristol-format gate-type tags (the 4th int per gate record).
enum GateType : int { AND_GATE = 0, XOR_GATE = 1, NOT_GATE = 2 };

}
#endif// CONFIG_H__
