#ifndef EMP_IR_SESSION_SESSION_H__
#define EMP_IR_SESSION_SESSION_H__

// The session-layer contract — the peer of BooleanContext (ir/context/concept.h).
// A Session is the protocol / I-O object an application drives; it owns I/O and
// protocol state, NOT circuit object construction. Circuit values are context-bound
// (UInt_T<Ctx,N> etc.) and a new value family is added without touching any session.
//
//   * Session         — the minimal protocol object: it reports a party().
//   * DirectSession   — a Session that exposes a direct / user gate context
//                       (ctx_t, a BooleanContext) for operator-mode circuits;
//                       ctx() returns it. A multipass protocol may own many
//                       internal pass contexts — ctx_t is only the direct one.
//   * CheckpointingSession — a Session that supports a checkpoint() barrier.
//
// Feeding / opening a value (input / reveal) is SessionIO<S,V>
// (ir/session/session_io.h), generic over any WireValue V — it names no concrete
// value family.
//
// Settlement (normative): a value returned by reveal is PROVISIONAL until the
// session's deferred protocol checks covering it have passed; each DirectSession
// must document its settlement point. There are two shapes: settle-at-reveal
// (checks flush before the cleartext is produced) and settle-at-finalize (reveal
// returns immediately, finalize() runs the closing checks). Plaintext/semi-honest
// sessions have nothing to settle.
// A failed check is a malicious abort and reports through error() — fatal, like
// every emp-tool failure (docs/api_conventions.md); there is no in-band error
// channel on reveal, by design.

#include "emp-tool/ir/context/concept.h"   // BooleanContext
#include <concepts>
#include <type_traits>

namespace emp {

template <class S>
concept Session =
    requires(const S& s) { { s.party() } -> std::convertible_to<int>; };

template <class S>
concept DirectSession =
    Session<S> &&
    requires(S& s) {
        typename S::ctx_t;
        requires BooleanContext<typename S::ctx_t>;
        { s.ctx() } -> std::same_as<typename S::ctx_t&>;
    };

template <class S>
concept CheckpointingSession =
    Session<S> &&
    requires(S& s) { s.checkpoint(); };

// Spelling helper so tests/docs can name a session's direct context without a
// session value alias: UInt_T<ctx_t<ClearSession>, 32>. In code that teaches
// the model, prefer the explicit `using Ctx = ClearSession::ctx_t;` form.
template <class S>
using ctx_t = typename std::remove_cvref_t<S>::ctx_t;

}  // namespace emp
#endif  // EMP_IR_SESSION_SESSION_H__
