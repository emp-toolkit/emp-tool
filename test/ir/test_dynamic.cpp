// Runtime-width session I/O (RuntimeWidthValue) over ClearSession: input/reveal of
// DynamicUInt_T / DynamicInt_T at runtime widths and
// owners, runtime-width operators, unsigned-vs-signed decode, malformed-codec /
// cross-session boundary rejection, and the zero-gate fixed<->dynamic conversions
// (to_dynamic / to_fixed<M> / resize). C++20.

#include "emp-tool/emp-tool.h"
#include "emp-tool/circuits/typed.h"
#include "emp-tool/ir/session/clear_session.h"
#include "emp-tool/ir/session/session_io.h"
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace emp;

static int bad = 0;
static void chk(const char* what, bool ok) { if (!ok) { printf("  [FAIL] %s\n", what); ++bad; } }

template <class F>
static bool dies(F&& f) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) dup2(dn, 2);
        f();
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return !(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

// Runtime-width codec with a deliberately short encode result. It models the
// structural concept so the session boundary, rather than template substitution,
// is what catches the extension bug.
template <class Ctx>
struct ShortRuntimeCodec {
    using Wire = typename Ctx::Wire;
    using context_type = Ctx;
    using clear_t = uint8_t;
    static constexpr bool is_dynamic = true;
    Ctx* ctx_ = nullptr;
    std::vector<Wire> wires;
    int width() const { return (int)wires.size(); }
    Ctx* context() const { return ctx_; }
    void pack_wires(Wire* out) const {
        for (std::size_t i = 0; i < wires.size(); ++i) out[i] = wires[i];
    }
    static ShortRuntimeCodec from_wires(Ctx& ctx, const Wire* in, int width) {
        ShortRuntimeCodec out;
        out.ctx_ = &ctx;
        out.wires.assign(in, in + width);
        return out;
    }
    static std::vector<uint8_t> encode(clear_t, int width) {
        return std::vector<uint8_t>((std::size_t)(width - 1));
    }
    static clear_t decode(const uint8_t*, int) { return 0; }
};

static_assert(RuntimeWidthValue<ShortRuntimeCodec<ClearCtx>>);

template <int N>
concept HasFixedUInt = requires { typename UInt_T<ClearCtx, N>; };
template <int N>
concept HasFixedInt = requires { typename Int_T<ClearCtx, N>; };
template <int N>
concept HasBitVec = requires { typename BitVec_T<ClearCtx, N>; };

static_assert(!HasFixedUInt<0> && !HasFixedUInt<-1>);
static_assert(!HasFixedInt<0> && !HasFixedInt<-1>);
static_assert(HasBitVec<0> && !HasBitVec<-1>);

// Two's-complement reference decode at runtime width w.
static int64_t twos(int64_t v, int w) {
    if (w >= 64) return v;
    uint64_t u = (uint64_t)v & ((1ull << w) - 1);
    if ((u >> (w - 1)) & 1) u |= ~((1ull << w) - 1);
    return (int64_t)u;
}

int main() {
    using Ctx = ClearSession::ctx_t;
    using RU  = DynamicUInt_T<Ctx>;
    using RI  = DynamicInt_T<Ctx>;

    // ---- fixed and runtime-width concept guards ----
    static_assert(WireValue<UInt_T<ClearCtx, 32>>,          "fixed UInt is WireValue");
    static_assert(WireValue<Int_T<ClearCtx, 32>>,           "fixed Int is WireValue");
    static_assert(!RuntimeWidthValue<UInt_T<ClearCtx, 32>>, "fixed is not RuntimeWidthValue");
    static_assert(RuntimeWidthValue<RU> && RuntimeWidthValue<RI>, "runtime types model RuntimeWidthValue");
    static_assert(std::default_initializable<RU> && std::default_initializable<RI>,
                  "dynamic integers have unbound assignment targets");
    static_assert(!WireValue<RU> && !WireValue<RI>,         "runtime types are not WireValue (no static width)");
    static_assert(SessionIO<ClearSession, UInt_T<ClearCtx, 32>>,  "fixed session I/O");
    static_assert(RuntimeSessionIO<ClearSession, RU>,       "ClearSession supports runtime unsigned I/O");
    static_assert(RuntimeSessionIO<ClearSession, RI>,       "ClearSession supports runtime signed I/O");

    ClearSession sess;

    // ---- session boundaries catch ownership and malformed runtime codecs ----
    chk("fixed reveal rejects a foreign session value", dies([] {
        ClearSession a, b;
        auto value = a.input<UInt_T<ClearCtx, 8>>(ALICE, 7);
        (void)b.reveal(value, PUBLIC);
    }));
    chk("runtime reveal rejects a foreign session value", dies([] {
        ClearSession a, b;
        auto value = a.input<DynamicUInt_T<ClearCtx>>(ALICE, 7, 8);
        (void)b.reveal(value, PUBLIC);
    }));
    chk("runtime construction rejects zero width", dies([] {
        ClearSession s;
        (void)DynamicUInt_T<ClearCtx>(s.ctx(), 0);
    }));
    chk("signed runtime construction rejects zero width", dies([] {
        ClearSession s;
        (void)DynamicInt_T<ClearCtx>(s.ctx(), 0);
    }));
    chk("runtime reveal rejects an unbound value", dies([] {
        ClearSession s;
        DynamicUInt_T<ClearCtx> value;
        (void)s.reveal(value, PUBLIC);
    }));
    chk("runtime arithmetic rejects mismatched widths", dies([] {
        ClearSession s;
        auto a = s.input<DynamicUInt_T<ClearCtx>>(ALICE, 1, 7);
        auto b = s.input<DynamicUInt_T<ClearCtx>>(BOB, 2, 8);
        (void)(a + b);
    }));
    chk("signed runtime arithmetic rejects mismatched widths", dies([] {
        ClearSession s;
        auto a = s.input<DynamicInt_T<ClearCtx>>(ALICE, -1, 7);
        auto b = s.input<DynamicInt_T<ClearCtx>>(BOB, 2, 8);
        (void)(a + b);
    }));
    chk("runtime resize rejects zero width", dies([] {
        ClearSession s;
        auto value = s.input<DynamicUInt_T<ClearCtx>>(ALICE, 1, 8);
        (void)value.resize(0);
    }));
    chk("runtime resize rejects negative width", dies([] {
        ClearSession s;
        auto value = s.input<DynamicInt_T<ClearCtx>>(ALICE, -1, 8);
        (void)value.resize(-1);
    }));
    chk("unsigned runtime codec rejects zero width", dies([] {
        (void)DynamicUInt_T<ClearCtx>::encode(7, 0);
    }));
    chk("unsigned runtime codec rejects negative width", dies([] {
        (void)DynamicUInt_T<ClearCtx>::encode(7, -1);
    }));
    chk("signed runtime codec rejects zero width", dies([] {
        uint8_t bit = 0;
        (void)DynamicInt_T<ClearCtx>::decode(&bit, 0);
    }));
    chk("signed runtime codec rejects negative width", dies([] {
        uint8_t bit = 0;
        (void)DynamicInt_T<ClearCtx>::decode(&bit, -1);
    }));
    chk("runtime input rejects a short codec result", dies([] {
        ClearSession s;
        (void)s.input<ShortRuntimeCodec<ClearCtx>>(ALICE, 0, 8);
    }));

    // ---- unsigned runtime: input/reveal + ops, several widths and owners ----
    for (int w : {1, 7, 16, 20, 33, 48, 64}) {
        const uint64_t mask = (w >= 64) ? ~0ull : ((1ull << w) - 1);
        const uint64_t x = 0xDEADBEEFCAFEull & mask, y = 0x1234567890ABull & mask;
        auto a = sess.input<RU>(ALICE, x, w);
        auto b = sess.input<RU>(BOB,   y, w);
        chk("uint runtime width()",   a.width() == w);
        chk("uint runtime reveal",    sess.reveal(a, PUBLIC).value() == x);
        chk("uint runtime +",         sess.reveal(a + b, PUBLIC).value() == ((x + y) & mask));
        chk("uint runtime -",         sess.reveal(a - b, PUBLIC).value() == ((x - y) & mask));
        chk("uint runtime ^",         sess.reveal(a ^ b, PUBLIC).value() == (x ^ y));
        chk("uint runtime ==",        ((a == a).w & 1) == 1 && (((a == b).w & 1) == (uint64_t)(x == y)));
        chk("uint runtime <",         ((a < b).w & 1) == (uint64_t)(x < y));
        chk("uint runtime PUBLIC in", sess.reveal(sess.input<RU>(PUBLIC, x, w), PUBLIC).value() == x);
    }

    // ---- signed runtime: decode sign-extends from the top bit of `width` ----
    for (int w : {8, 16, 32, 48}) {
        for (int64_t v : { (int64_t)-1, (int64_t)-1000, (int64_t)5, (int64_t)123456 }) {
            auto s = sess.input<RI>(ALICE, v, w);
            chk("int runtime signed reveal", sess.reveal(s, PUBLIC).value() == twos(v, w));
        }
        auto p = sess.input<RI>(ALICE, -100, w);
        auto q = sess.input<RI>(BOB,     30, w);
        chk("int runtime +",        sess.reveal(p + q, PUBLIC).value() == -70);
        chk("int runtime neg < pos", ((p < q).w & 1) == 1);
    }

    // ---- same top-bit-set bits decode differently by signedness ----
    {
        const int w = 8;
        chk("uint zero-extends", sess.reveal(sess.input<RU>(ALICE, 0xF0u, w), PUBLIC).value() == 0xF0u);
        chk("int sign-extends",  sess.reveal(sess.input<RI>(ALICE, (int64_t)0xF0, w), PUBLIC).value()
                                     == (int64_t)(int8_t)0xF0);
    }

    // ---- zero-gate fixed <-> dynamic conversions ----
    {
        const uint32_t V = 0xABCD1234u;
        auto f = sess.input<UInt_T<Ctx, 32>>(ALICE, V);   // fixed input (unchanged path)
        RU d = f.to_dynamic();
        chk("to_dynamic width/value", d.width() == 32 && sess.reveal(d, PUBLIC).value() == V);
        auto back = d.to_fixed<32>();
        chk("to_fixed value",         sess.reveal(back, PUBLIC).value() == V);
        auto narrow = d.to_fixed<16>();
        chk("to_fixed truncates",      sess.reveal(narrow, PUBLIC).value() == (V & 0xFFFFu));
        RU up = d.resize(40);
        chk("resize zero-extend",     up.width() == 40 && sess.reveal(up, PUBLIC).value() == V);
        RU dn = d.resize(16);
        chk("resize truncate",        dn.width() == 16 && sess.reveal(dn, PUBLIC).value() == (V & 0xFFFFu));
        chk("dynamic reinterpret round-trip",
            sess.reveal(d.as_signed().as_unsigned(), PUBLIC).value() == V);
    }

    // ---- signed fixed <-> dynamic conversions preserve sign extension ----
    {
        auto f = sess.input<Int_T<Ctx, 16>>(ALICE, -1234);
        RI d = f.to_dynamic();
        chk("signed to_dynamic width/value",
            d.width() == 16 && sess.reveal(d, PUBLIC).value() == -1234);
        auto wide = d.to_fixed<32>();
        chk("signed to_fixed sign-extends", sess.reveal(wide, PUBLIC).value() == -1234);
        auto narrow = d.to_fixed<8>();
        chk("signed to_fixed truncates",
            sess.reveal(narrow, PUBLIC).value() == 46);
        chk("signed dynamic reinterpret round-trip",
            sess.reveal(d.as_unsigned().as_signed(), PUBLIC).value() == -1234);
    }

    printf("test_dynamic: %s\n", bad ? "FAILED" : "runtime-width session I/O — PASS");
    return bad ? 1 : 0;
}
