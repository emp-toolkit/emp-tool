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
#include "emp-tool/io/net_io_buffered_channel.h"

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

// circuits/circuit.h aggregates all emp-tool/circuits/*.h headers, exposes
// the block-typed default aliases (Bit, BitVec, UnsignedInt, …, UInt64,
// Int64, AES_*, SHA3_*), and declares `extern template` for the common
// instantiations. The matching `template class …;` definitions live in
// circuits/circuit.cpp so each TU only links to the symbols.
#include "emp-tool/circuits/circuit.h"
