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

#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/circuit_file.h"
#include "emp-tool/circuits/comparable.h"
#include "emp-tool/circuits/float32.h"
#include "emp-tool/circuits/integer.h"
#include "emp-tool/circuits/number.h"
#include "emp-tool/circuits/swappable.h"
#include "emp-tool/circuits/sha3_256.h"
#include "emp-tool/circuits/aes_128_ctr.h"

#include "emp-tool/core/block.h"
#include "emp-tool/core/constants.h"
#include "emp-tool/crypto/hash.h"
#include "emp-tool/crypto/prg.h"
#include "emp-tool/crypto/prp.h"
#include "emp-tool/crypto/crh.h"
#include "emp-tool/crypto/ccrh.h"
#include "emp-tool/crypto/tccrh.h"
#include "emp-tool/core/utils.h"
#include "emp-tool/third_party/ThreadPool.h"
#include "emp-tool/group/group.h"
#include "emp-tool/crypto/mitccrh.h"
#include "emp-tool/crypto/aes.h"
#include "emp-tool/crypto/f2k.h"

#include "emp-tool/gc/halfgate_eva.h"
#include "emp-tool/gc/halfgate_gen.h"
#include "emp-tool/gc/privacy_free_eva.h"
#include "emp-tool/gc/privacy_free_gen.h"

#include "emp-tool/execution/circuit_execution.h"
#include "emp-tool/execution/protocol_execution.h"
#include "emp-tool/execution/plain_circ.h"
#include "emp-tool/execution/plain_prot.h"
