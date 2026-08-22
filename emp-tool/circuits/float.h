#ifndef EMP_CIRCUIT_FLOAT_H__
#define EMP_CIRCUIT_FLOAT_H__

// Float_T<Ctx,W>: IEEE binary{16,32,64} over a BooleanContext. Arithmetic replays
// the recorded fp<W>_<op>.empbc builtins through the context (the "big circuits ->
// IR replay" rule). The clear codec uses the host scalar (float for fp16/fp32,
// double for fp64) via FloatTraits<W>; raw-bit helpers are also provided.

#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/numeric_kernels.h"   // kernel::mux for select
#include "emp-tool/circuits/float_traits.h"      // FloatTraits<W>
#include "emp-tool/ir/builtins.h"                // circuit::float_circuit
#include "emp-tool/ir/execute.h"                 // execute_program (replay float .empbc)
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace emp {

template <BooleanContext Ctx, int W>
class Float_T {
public:
    using Wire         = typename Ctx::Wire;
    using context_type = Ctx;
    using host_t       = typename FloatTraits<W>::host_t;
    using clear_t      = host_t;
    template <BooleanContext C2> using rebind = Float_T<C2, W>;
    std::array<Wire, W> w{};

    Float_T() = default;
    explicit Float_T(Ctx& c) : ctx_(&c) {}
    static Float_T from_bits(Ctx& c, uint64_t bits) {
        Float_T r(c); for (int i = 0; i < W; ++i) r.w[i] = c.public_bit((bits >> i) & 1); return r;
    }
    static Float_T from_wires(Ctx& c, const Wire* in) {
        Float_T r(c); for (int i = 0; i < W; ++i) r.w[i] = in[i]; return r;
    }

    // Construct from a host value (e.g. 1.5f) via the per-width bit pattern.
    static Float_T constant(Ctx& c, host_t v) { return from_bits(c, FloatTraits<W>::to_bits(v)); }

    Ctx*    context() const { return ctx_; }
    Float_T constant(host_t v) const { return constant(*ctx_, v); }   // same-context sugar

    // ---- IR-replay helpers over the fp<W>_<op>.empbc builtins ----
    // unary/binary/ternary return W bits; compare/classify circuits emit 8 bits
    // (bit 0 is the result), matching the recorded suite. Replay reuses a
    // thread_local ProgramWorkspace. Trivially copyable wires remain in the
    // workspace; other wires are moved out and live workspace elements cleared.
private:
    template <int K>
    void replay_(const char* op, const std::array<const Float_T*, K>& ops,
                 Wire* result, std::size_t result_size) const {
        static thread_local ProgramWorkspace<Wire> ws;
        ws.tmp_inputs.resize((size_t)K * W);
        for (int k = 0; k < K; ++k)
            for (int i = 0; i < W; ++i) ws.tmp_inputs[(size_t)k * W + i] = ops[k]->w[i];
        const auto& out = execute_program(
            *ctx_, circuit::float_circuit(W, op),
            std::span<const Wire>(ws.tmp_inputs.data(), (size_t)K * W), ws);
        if constexpr (std::is_trivially_copyable_v<Wire>) {
            if (result_size == 1)
                result[0] = out[0];
            else
                std::memcpy(result, out.data(), (std::size_t)W * sizeof(Wire));
        } else {
            if (result_size == 1)
                result[0] = std::move(ws.out[0]);
            else
                for (int i = 0; i < W; ++i)
                    result[i] = std::move(ws.out[(std::size_t)i]);
            ws.release_wire_values();
        }
    }
    Float_T unary_(const char* op) const {
        Float_T r(*ctx_); replay_<1>(op, {this}, r.w.data(), r.w.size()); return r;
    }
    Float_T binop_(const char* op, const Float_T& o) const {
        check_same_context(*this, o);
        Float_T r(*ctx_); replay_<2>(op, {this, &o}, r.w.data(), r.w.size()); return r;
    }
    Float_T ternary_(const char* op, const Float_T& b, const Float_T& c) const {
        check_same_context(*this, b); check_same_context(*this, c);
        Float_T r(*ctx_); replay_<3>(op, {this, &b, &c}, r.w.data(), r.w.size()); return r;
    }
    Bit_T<Ctx> cmp_(const char* op, const Float_T& o) const {
        check_same_context(*this, o);
        std::array<Wire, 1> out{};
        replay_<2>(op, {this, &o}, out.data(), out.size());
        return Bit_T<Ctx>(*ctx_, out[0]);
    }
    Bit_T<Ctx> classify_(const char* op) const {
        std::array<Wire, 1> out{};
        replay_<1>(op, {this}, out.data(), out.size());
        return Bit_T<Ctx>(*ctx_, out[0]);
    }

public:
    // ---- arithmetic ----
    Float_T operator+(const Float_T& o) const { return binop_("add", o); }
    Float_T operator-(const Float_T& o) const { return binop_("sub", o); }
    Float_T operator*(const Float_T& o) const { return binop_("mul", o); }
    Float_T operator/(const Float_T& o) const { return binop_("div", o); }
    Float_T min(const Float_T& o) const { return binop_("min", o); }
    Float_T max(const Float_T& o) const { return binop_("max", o); }
    Float_T sqr()   const { return unary_("square"); }
    Float_T sqrt()  const { return unary_("sqrt"); }
    Float_T recip() const { return unary_("recip"); }
    Float_T rsqrt() const { return unary_("rsqrt"); }
    Float_T fma(const Float_T& b, const Float_T& c) const { return ternary_("fma", b, c); }

    // ---- comparisons / classifiers -> Bit_T ----
    Bit_T<Ctx> equal(const Float_T& o)         const { return cmp_("eq", o); }
    Bit_T<Ctx> not_equal(const Float_T& o)     const { return cmp_("ne", o); }
    Bit_T<Ctx> less_than(const Float_T& o)     const { return cmp_("lt", o); }
    Bit_T<Ctx> less_equal(const Float_T& o)    const { return cmp_("le", o); }
    Bit_T<Ctx> greater_than(const Float_T& o)  const { return cmp_("gt", o); }
    Bit_T<Ctx> greater_equal(const Float_T& o) const { return cmp_("ge", o); }
    Bit_T<Ctx> operator==(const Float_T& o) const { return equal(o); }
    Bit_T<Ctx> operator!=(const Float_T& o) const { return not_equal(o); }
    Bit_T<Ctx> operator<(const Float_T& o)  const { return less_than(o); }
    Bit_T<Ctx> operator<=(const Float_T& o) const { return less_equal(o); }
    Bit_T<Ctx> operator>(const Float_T& o)  const { return greater_than(o); }
    Bit_T<Ctx> operator>=(const Float_T& o) const { return greater_equal(o); }
    Bit_T<Ctx> is_nan()  const { return classify_("isnan"); }
    Bit_T<Ctx> is_inf()  const { return classify_("isinf"); }
    Bit_T<Ctx> is_zero() const { return classify_("iszero"); }

    // ---- sign-bit ops + select: pure wiring, realized locally (MSB = bit W-1) ----
    Float_T abs() const        { Float_T r(*this); r.w[W - 1] = ctx_->public_bit(false); return r; }
    Float_T operator-() const  { Float_T r(*this); r.w[W - 1] = ctx_->not_gate(w[W - 1]); return r; }
    Float_T copysign(const Float_T& o) const { check_same_context(*this, o); Float_T r(*this); r.w[W - 1] = o.w[W - 1]; return r; }
    Float_T select(const Bit_T<Ctx>& sel, const Float_T& o) const {
        check_same_context(*this, sel); check_same_context(*this, o);
        Float_T r(*ctx_); for (int i = 0; i < W; ++i) r.w[i] = kernel::mux(*ctx_, sel.w, o.w[i], w[i]); return r;
    }
    Bit_T<Ctx> operator[](int i) const { return Bit_T<Ctx>(*ctx_, w[i]); }

    static constexpr int width() { return W; }
    void pack_wires(Wire* out) const { for (int i = 0; i < W; ++i) out[i] = w[i]; }
    static std::array<bool, (std::size_t)W> encode(host_t v) {
        return encode_bits(FloatTraits<W>::to_bits(v));
    }
    static host_t decode(const bool* bits) {
        uint64_t v = 0; for (int i = 0; i < W; ++i) v |= (uint64_t)(bits[i] ? 1 : 0) << i;
        return FloatTraits<W>::from_bits(v);
    }
    // Raw-bit helpers (when you want the IEEE pattern directly).
    static std::array<bool, (std::size_t)W> encode_bits(uint64_t bits) {
        std::array<bool, (std::size_t)W> b{};
        for (int i = 0; i < W; ++i) b[(std::size_t)i] = (bits >> i) & 1; return b;
    }
    static uint64_t decode_bits(const bool* bits) {
        uint64_t v = 0; for (int i = 0; i < W; ++i) v |= (uint64_t)(bits[i] ? 1 : 0) << i; return v;
    }

private:
    Ctx* ctx_ = nullptr;
};

}  // namespace emp
#endif  // EMP_CIRCUIT_FLOAT_H__
