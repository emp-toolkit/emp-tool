#ifndef EMP_RUNTIME_RUNTIME_H__
#define EMP_RUNTIME_RUNTIME_H__

// runtime — the low-level substrate: core (block / utils / constants / test_mode /
// simd_tier), the runtime crypto primitives, the IO channels, and the garbling leaf
// primitives (half-gate / privacy-free). It defines no circuit values, no IR, and
// no sessions; the ir and circuits layers build on it.

// emp-tool's wire formats and most internal byte/bit packings assume little-endian.
// KDF (hash.h), block packing (block.h), and any IO that memcpy's an integer over
// the wire would silently produce mismatched output on a big-endian peer. Refuse to
// build there rather than ship a binary that subtly disagrees with little-endian
// counterparties.
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "emp-tool requires a little-endian target");

#include <thread>
#include "emp-tool/runtime/io/io_channel.h"
#include "emp-tool/runtime/io/net_io_channel.h"
#include "emp-tool/runtime/io/tls_io_channel.h"
#include "emp-tool/runtime/io/trace_io.h"

#include "emp-tool/runtime/core/block.h"
#include "emp-tool/runtime/core/block_vector.h"
#include "emp-tool/runtime/core/constants.h"
#include "emp-tool/runtime/core/test_mode.h"
#include "emp-tool/runtime/core/utils.h"
#include "emp-tool/runtime/crypto/hash.h"
#include "emp-tool/runtime/crypto/prg.h"
#include "emp-tool/runtime/crypto/prp.h"
#include "emp-tool/runtime/crypto/ccrh.h"
#include "emp-tool/runtime/crypto/mitccrh.h"
#include "emp-tool/runtime/crypto/aes.h"
#include "emp-tool/runtime/crypto/session_id.h"
#include "emp-tool/runtime/crypto/f2k.h"
#include "emp-tool/runtime/crypto/ec.h"
#include "emp-tool/runtime/crypto/ro.h"
// Exported for protocol libraries that thread their passes and IO over it;
// nothing in runtime/ itself uses ThreadPool.
#include "emp-tool/third_party/ThreadPool.h"

#include "emp-tool/runtime/execution/half_gate.h"
#include "emp-tool/runtime/execution/privacy_free.h"

#endif  // EMP_RUNTIME_RUNTIME_H__
