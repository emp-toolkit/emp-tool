// Session-layer contracts: ClearSession models Session / DirectSession / SessionIO
// (but not CheckpointingSession); WireValue is fixed-width only; and a brand-new
// value family is input/revealed through SessionIO with NO edit to the session —
// proving value families are decoupled from sessions. Single-process compile gate +
// a plaintext round-trip.
#include "emp-tool/emp-tool.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <type_traits>
#include <vector>
using namespace emp;

// ---- ClearSession models the session concepts ----
static_assert(Session<ClearSession>);
static_assert(DirectSession<ClearSession>);
static_assert(std::is_same_v<ClearSession::ctx_t, ClearCtx>);
static_assert(SessionIO<ClearSession, UInt_T<ClearCtx, 32>>);
static_assert(SessionIO<ClearSession, Int_T<ClearCtx, 32>>);
static_assert(SessionIO<ClearSession, BitVec_T<ClearCtx, 128>>);
static_assert(!CheckpointingSession<ClearSession>);   // ClearSession has no checkpoint()

// ---- WireValue is fixed-width only: runtime-width values are NOT WireValue ----
static_assert(WireValue<UInt_T<ClearCtx, 32>>);
static_assert(!WireValue<UInt_T<ClearCtx, runtime_width>>);   // runtime-width form
static_assert(!WireValue<Int_T<ClearCtx, runtime_width>>);

// ---- WireBundle vs WireValue: execution needs wires, session I/O needs the
// codec. The integer clear codec rides a 64-bit scalar, so a wider UInt/Int is
// a WireBundle (a fine circuit argument) but NOT a WireValue: the concept
// rejects it rather than accepting it and failing at instantiation.
static_assert(WireBundle<UInt_T<ClearCtx, 32>>);              // every WireValue is a WireBundle
static_assert(WireBundle<UInt_T<ClearCtx, 128>>);
static_assert(!WireValue<UInt_T<ClearCtx, 128>>);             // no 128-bit clear codec
static_assert(WireBundle<Int_T<ClearCtx, 128>>);
static_assert(!WireValue<Int_T<ClearCtx, 128>>);
static_assert(WireValue<UInt_T<ClearCtx, 64>>);               // the codec boundary
static_assert(WireValue<BitVec_T<ClearCtx, 128>>);            // the wide typed-I/O family
static_assert(!WireBundle<UInt_T<ClearCtx, runtime_width>>);  // runtime width models neither

// A codec-less synthetic family: structural members only — a WireBundle (usable
// by the frontend / execution) that is NOT a WireValue and NOT session-feedable.
template <class Ctx>
struct Raw3 {
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    template <class C2> using rebind = Raw3<C2>;
    Ctx* ctx_ = nullptr;
    Wire w[3]{};
    Ctx* context() const { return ctx_; }
    static constexpr int width() { return 3; }
    void pack_wires(Wire* out) const { for (int i = 0; i < 3; ++i) out[i] = w[i]; }
    static Raw3 from_wires(Ctx& c, const Wire* in) { Raw3 r; r.ctx_ = &c; for (int i = 0; i < 3; ++i) r.w[i] = in[i]; return r; }
};
static_assert(WireBundle<Raw3<ClearCtx>>);
static_assert(!WireValue<Raw3<ClearCtx>>);
static_assert(!SessionIO<ClearSession, Raw3<ClearCtx>>);      // no codec, no session I/O

// ---- A synthetic value family: a 2-bit value over any BooleanContext. It is a
// WireValue purely by its own static members, names no existing family, and is
// usable through SessionIO without touching ClearSession. ----
template <class Ctx>
struct Pair2 {
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using clear_t      = uint8_t;                       // value in [0,3]
    template <class C2> using rebind = Pair2<C2>;

    Ctx* ctx_ = nullptr;
    Wire w0{}, w1{};

    Ctx* context() const { return ctx_; }
    static constexpr int width() { return 2; }
    void pack_wires(Wire* out) const { out[0] = w0; out[1] = w1; }
    static Pair2 from_wires(Ctx& c, const Wire* in) { Pair2 p; p.ctx_ = &c; p.w0 = in[0]; p.w1 = in[1]; return p; }
    static std::array<bool, 2> encode(clear_t v) { return { (bool)(v & 1), (bool)((v >> 1) & 1) }; }
    static clear_t decode(const bool* b) { return (clear_t)((b[0] ? 1 : 0) | (b[1] ? 2 : 0)); }
};

static_assert(WireValue<Pair2<ClearCtx>>);
static_assert(SessionIO<ClearSession, Pair2<ClearCtx>>);   // no ClearSession edit was needed

int main() {
    ClearSession sess;
    bool ok = true;

    // The synthetic value family round-trips through the session's I/O surface.
    auto p = sess.input<Pair2<ClearCtx>>(ALICE, (uint8_t)2);
    std::optional<uint8_t> rp = sess.reveal(p, PUBLIC);
    ok &= rp.has_value() && rp.value() == 2;

    // A normal value family does too — same surface, no session knowledge of UInt.
    auto a = sess.input<UInt_T<ClearCtx, 32>>(ALICE, (uint64_t)7);
    auto b = sess.input<UInt_T<ClearCtx, 32>>(BOB, (uint64_t)5);
    std::optional<uint64_t> rs = sess.reveal(a + b, PUBLIC);
    ok &= rs.has_value() && rs.value() == 12;

    // The session contract is n-party: owner/recipient are plain party ids >= 1
    // (constants.h), so the plaintext oracle serves multi-party protocols too —
    // a 5-party round-trip with reveal to party 4 works with no session edit.
    {
        using U16 = UInt_T<ClearCtx, 16>;
        auto acc = U16::constant(sess.ctx(), 0);
        for (int party = 1; party <= 5; ++party)
            acc = acc + sess.input<U16>(party, (uint64_t)(10 * party));
        std::optional<uint64_t> rn = sess.reveal(acc, 4);
        ok &= rn.has_value() && rn.value() == 150;
    }

    std::printf("test_session_concepts: %s\n", ok ? "GOOD!" : "BAD!");
    return ok ? 0 : 1;
}
