// emp-tool's wire formats and most internal byte/bit packings assume
// little-endian. KDF (hash.h), block packing (block.h), and any IO that
// memcpy's an integer over the wire would silently produce mismatched
// output on a big-endian peer. Refuse to build there rather than ship a
// binary that subtly disagrees with little-endian counterparties.
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "emp-tool requires a little-endian target");

#include <thread>
#include "emp-tool/io/io_channel.h"
#include "emp-tool/io/mem_io_channel.h"
#include "emp-tool/io/net_io_channel.h"

#include "emp-tool/core/block.h"
#include "emp-tool/core/constants.h"
#include "emp-tool/core/utils.h"
#include "emp-tool/crypto/hash.h"
#include "emp-tool/crypto/prg.h"
#include "emp-tool/crypto/prp.h"
#include "emp-tool/crypto/ccrh.h"
#include "emp-tool/crypto/mitccrh.h"
#include "emp-tool/crypto/aes.h"
#include "emp-tool/crypto/f2k.h"
#include "emp-tool/group/group.h"
#include "emp-tool/third_party/ThreadPool.h"

#include "emp-tool/execution/backend.h"
#include "emp-tool/execution/clear_backend.h"
#include "emp-tool/execution/half_gate.h"
#include "emp-tool/execution/privacy_free.h"

#include "emp-tool/circuits/sortable.h"
#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/integer.h"
#include "emp-tool/circuits/float32.h"
#include "emp-tool/circuits/circuit_file.h"
#include "emp-tool/circuits/sha3_256.h"
#include "emp-tool/circuits/aes_128_ctr.h"

namespace emp {
// Convenience aliases at the umbrella layer. The class definitions in
// circuits/* are agnostic to wire type; this is the spot where emp-tool
// commits to `block` as the default wire (matching the wire type used by
// every backend emp-tool ships: ClearBackend, HalfGate*, PrivacyFree*).
// Code that wants a different wire type instantiates Bit_T<W> / Integer_T<W>
// / Float_T<W> directly and skips these aliases.
using Bit     = Bit_T<block>;
using Integer = Integer_T<block>;
using Float   = Float_T<block>;
using AES_128_CTR_Calculator = AES_128_CTR_Calculator_T<block>;
using SHA3_256_Calculator    = SHA3_256_Calculator_T<block>;
}
