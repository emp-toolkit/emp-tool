#ifndef EMP_IR_SESSION_SESSION_IO_H__
#define EMP_IR_SESSION_SESSION_IO_H__

// SessionIO<S,V> — a DirectSession S can feed (input) and open (reveal) a value V
// across its direct context. Generic over any WireValue V; it names no concrete
// value family, so a new value family is usable through any session without editing
// the session. reveal is recipient-explicit: reveal_t<V> is std::optional<clear_t>,
// the value present only on a party that learns it (every party for PUBLIC, the
// named recipient otherwise) and std::nullopt on a party that does not.
//
// owner / recipient take the party vocabulary defined ONCE in
// runtime/core/constants.h: PUBLIC, or any party id >= 1 — the contract is
// n-party (ALICE/BOB merely name ids 1 and 2). A session accepts the subset its
// protocol can serve and error()s on the rest (e.g. the XOR-share reveal
// sentinel is honored only by sessions that document it). A revealed value is
// provisional until the session's documented settlement point — see session.h.

#include "emp-tool/ir/session/session.h"   // Session / DirectSession / CheckpointingSession
#include "emp-tool/ir/wire_value.h"        // WireValue
#include <concepts>
#include <optional>

namespace emp {

// Codec storage follows docs/api_conventions.md's bit-buffer contract; width
// is enforced by the WireValue encode return type.
template <class S, class V>
concept SessionIO =
    DirectSession<S> &&
    WireValue<V> &&
    std::same_as<typename V::context_type, typename S::ctx_t> &&
    requires(S& s, typename V::clear_t x, const V& v, int party) {
        typename S::template reveal_t<V>;
        requires std::same_as<typename S::template reveal_t<V>,
                              std::optional<typename V::clear_t>>;
        { s.template input<V>(party, x) } -> std::same_as<V>;
        { s.reveal(v, party) } -> std::same_as<typename S::template reveal_t<V>>;
    };

// RuntimeSessionIO<S,V> — the runtime-width sibling of SessionIO: a DirectSession S
// can feed (input, with an explicit runtime width) and open (reveal) a
// RuntimeWidthValue V. reveal needs no width — it reads V::width() off the value.
// A session models this only if it supports runtime-width I/O (e.g. ClearSession);
// a session with fixed-width I/O only does not, which makes the capability a
// discoverable trait rather than a dead method.
template <class S, class V>
concept RuntimeSessionIO =
    DirectSession<S> &&
    RuntimeWidthValue<V> &&
    std::same_as<typename V::context_type, typename S::ctx_t> &&
    requires(S& s, typename V::clear_t x, const V& v, int party, int width) {
        typename S::template reveal_t<V>;
        requires std::same_as<typename S::template reveal_t<V>,
                              std::optional<typename V::clear_t>>;
        { s.template input<V>(party, x, width) } -> std::same_as<V>;
        { s.reveal(v, party) } -> std::same_as<typename S::template reveal_t<V>>;
    };

}  // namespace emp
#endif  // EMP_IR_SESSION_SESSION_IO_H__
