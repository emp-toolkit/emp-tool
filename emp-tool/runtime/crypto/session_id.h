#ifndef EMP_SESSION_ID_H__
#define EMP_SESSION_ID_H__
#include "emp-tool/runtime/crypto/aes.h"
#include "emp-tool/runtime/core/block.h"
#include "emp-tool/runtime/core/constants.h"
#include <cstdint>

namespace emp {

// A session identifier plus an internal child-derivation counter.
//
// A protocol that spawns sub-protocols holds one SessionID and calls
// derive() once per child, in a fixed construction order. Each child is
// AES_{sid}(counter), with the counter advancing per call, so siblings get
// distinct, mutually unrelated ids. The derivation is a pure function of
// (parent id, counter): two endpoints that construct their children in the
// same order obtain identical child ids with no value crossing the wire,
// and it is independent of any test/determinism mode.
//
// Two access verbs encode the intended use:
//   value()  — the raw id, bound directly by a leaf (e.g. into a hash) or
//              forwarded to a component that shares this session.
//   derive() — a fresh child id for a separately-constructed sub-protocol;
//              advances the counter.
//
// Copying preserves both the identifier and derivation counter, so equal
// copies produce the same child sequence until advanced independently.
//
// The block ctor is implicit so a caller can seed a top-level SessionID
// from a plain id. Default construction yields the zero id (counter 0).
class SessionID {
public:
  SessionID() = default;
  SessionID(block sid) : sid_(sid) {}   // implicit: seed from a raw id

  block value() const { return sid_; }

  [[nodiscard]] SessionID derive() {
    AES_KEY k;
    AES_set_encrypt_key(sid_, &k);
    block in = makeBlock(0, counter_++);
    AES_ecb_encrypt_blks<1>(&in, &k);
    return SessionID(in);
  }

private:
  block    sid_     = zero_block;
  uint64_t counter_ = 0;
};

}  // namespace emp
#endif  // EMP_SESSION_ID_H__
