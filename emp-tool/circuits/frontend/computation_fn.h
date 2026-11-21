#ifndef EMP_FRONTEND_COMPUTATION_FN_H__
#define EMP_FRONTEND_COMPUTATION_FN_H__

// Circuit mode, typed. A Computation gives a compiled Circuit<RetV, ArgVs...>
// (circuit_fn.h) a role for its return value and for each argument. A session
// that models ComputationSession (ir/session/computation_session.h) prepares it
// offline, knowing every role, and runs it online:
//
//   namespace cf = emp::frontend;
//   auto comp = cf::computation(circ, cf::to(BOB), cf::from(ALICE), cf::from(PUBLIC));
//   auto [out, a_in, k_in] = comp.handles();   // the return's handle, then one per argument
//   auto job = cf::prepare(sess, comp);         // offline
//   auto res = cf::run(sess, std::move(job),    // online; at ALICE: her input and every Public one
//                      cf::bind(a_in, x), cf::bind(k_in, k));
//   std::optional<uint32_t> v = res[out];       // present where this party receives the output
//
// Roles: cf::to(r) for the return and cf::from(o) for each argument, each a party
// id >= 1 or PUBLIC; every role must be given. Party and Public slots need a
// value type with a clear codec (WireValue). Circuit keeps one return value, so
// a typed computation has one output slot; a computation with several output
// slots uses the raw surface (a ComputationArtifact with prepare_computation /
// run_computation).
//
// Identity: a handle carries its value type at compile time, and its
// computation and slot at run time; a job records the session that prepared
// it. Running a job on another session, running it twice, binding a handle of
// another computation and binding a slot twice are fatal, before any traffic.
// Which slots a party binds is the session's binding rule (check_bindings): its
// own Party slots and every Public slot.

#include "emp-tool/circuits/frontend/circuit_fn.h"   // Circuit, RecordValue
#include "emp-tool/ir/computation.h"                  // ComputationArtifact, validate_computation
#include "emp-tool/ir/session/computation_session.h"  // ComputationSession, RawBindings / RawResult
#include "emp-tool/ir/wire_value.h"                   // WireValue
#include "emp-tool/runtime/core/constants.h"          // PUBLIC
#include "emp-tool/runtime/core/utils.h"              // expecting
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace emp {
namespace frontend {

// ---------------------------------------------------------------------------
// Roles for a Circuit's return value (to) and its arguments (from).
// ---------------------------------------------------------------------------
struct OutputRoleArg { int recipient; };
struct InputRoleArg  { int owner; };
inline OutputRoleArg to(int recipient) { return OutputRoleArg{recipient}; }
inline InputRoleArg  from(int owner)   { return InputRoleArg{owner}; }

template <RecordValue RetV, RecordValue... ArgVs> class Computation;

namespace detail {
inline uint64_t next_computation_id() {
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}
}  // namespace detail

// ---------------------------------------------------------------------------
// Handles name one slot of one computation.
// ---------------------------------------------------------------------------
template <RecordValue V>
class InputHandle {
public:
    uint64_t computation() const { return computation_; }
    uint32_t slot() const { return slot_; }

private:
    template <RecordValue R, RecordValue... A> friend class Computation;
    InputHandle(uint64_t c, uint32_t s) : computation_(c), slot_(s) {}
    uint64_t computation_;
    uint32_t slot_;
};

template <RecordValue V>
class OutputHandle {
public:
    uint64_t computation() const { return computation_; }
    uint32_t slot() const { return slot_; }

private:
    template <RecordValue R, RecordValue... A> friend class Computation;
    OutputHandle(uint64_t c, uint32_t s) : computation_(c), slot_(s) {}
    uint64_t computation_;
    uint32_t slot_;
};

// One input value, encoded, with the slot it binds.
template <RecordValue V>
struct Binding {
    uint64_t computation;
    uint32_t slot;
    std::array<bool, (std::size_t)V::width()> bits;
};

template <class T> inline constexpr bool is_binding_v = false;
template <RecordValue V> inline constexpr bool is_binding_v<Binding<V>> = true;

template <RecordValue V>
Binding<V> bind(const InputHandle<V>& h, const typename V::clear_t& value) {
    static_assert(WireValue<V>, "frontend::bind: the slot's value type needs a clear codec (WireValue)");
    return Binding<V>{h.computation(), h.slot(), V::encode(value)};
}

// ---------------------------------------------------------------------------
// Computation<RetV, ArgVs...>: a Circuit plus roles, as an immutable shared
// ComputationArtifact. Copies share the artifact and the identity.
// ---------------------------------------------------------------------------
template <RecordValue RetV, RecordValue... ArgVs>
class Computation {
public:
    using ret_value = RetV;
    static constexpr std::size_t arity = sizeof...(ArgVs);

    // Builds and validates the artifact; cf::computation is the usual spelling.
    Computation(const Circuit<RetV, ArgVs...>& c, OutputRoleArg ret,
                const std::array<InputRoleArg, sizeof...(ArgVs)>& args)
        : id_(detail::next_computation_id()) {
        auto a = std::make_shared<circuit::ComputationArtifact>();
        a->program = c.program_shared();
        const std::array<uint32_t, sizeof...(ArgVs)> widths{(uint32_t)ArgVs::width()...};
        for (std::size_t i = 0; i < sizeof...(ArgVs); ++i)
            a->io.inputs.push_back(args[i].owner == PUBLIC
                ? circuit::InputRole{widths[i], circuit::InputKind::Public, PUBLIC}
                : circuit::InputRole{widths[i], circuit::InputKind::Party, args[i].owner});
        a->io.outputs.push_back(ret.recipient == PUBLIC
            ? circuit::OutputRole{(uint32_t)RetV::width(), circuit::OutputKind::Public, PUBLIC}
            : circuit::OutputRole{(uint32_t)RetV::width(), circuit::OutputKind::Party, ret.recipient});
        circuit::validate_computation(*a);
        artifact_ = std::move(a);
    }

    // The return value's handle, then one handle per argument, in order.
    std::tuple<OutputHandle<RetV>, InputHandle<ArgVs>...> handles() const {
        return handles_(std::index_sequence_for<ArgVs...>{});
    }

    const std::shared_ptr<const circuit::ComputationArtifact>& artifact() const { return artifact_; }
    uint64_t id() const { return id_; }

private:
    template <std::size_t... I>
    std::tuple<OutputHandle<RetV>, InputHandle<ArgVs>...> handles_(std::index_sequence<I...>) const {
        return {OutputHandle<RetV>(id_, 0), InputHandle<ArgVs>(id_, (uint32_t)I)...};
    }

    std::shared_ptr<const circuit::ComputationArtifact> artifact_;
    uint64_t id_ = 0;
};

// cf::computation(circ, cf::to(recipient), cf::from(owner)...): one from() per
// argument, in order.
template <RecordValue RetV, RecordValue... ArgVs, class... Roles>
Computation<RetV, ArgVs...> computation(const Circuit<RetV, ArgVs...>& c, OutputRoleArg ret,
                                        Roles... args) {
    static_assert(sizeof...(Roles) == sizeof...(ArgVs),
        "frontend::computation: give one cf::from(owner) per circuit argument");
    static_assert((std::same_as<Roles, InputRoleArg> && ...),
        "frontend::computation: each argument's role is cf::from(owner)");
    static_assert(WireValue<RetV> && (WireValue<ArgVs> && ...),
        "frontend::computation: Party and Public slots need value types with a clear codec (WireValue)");
    return Computation<RetV, ArgVs...>(c, ret, std::array<InputRoleArg, sizeof...(ArgVs)>{args...});
}

// ---------------------------------------------------------------------------
// Job: a prepared, single-use run of one computation on one session.
// ---------------------------------------------------------------------------
template <ComputationSession S, RecordValue RetV, RecordValue... ArgVs>
class Job {
public:
    // cf::prepare is the usual spelling.
    Job(S* session, uint64_t computation, typename S::job_t&& inner)
        : session_(session), computation_(computation), inner_(std::move(inner)) {}

    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;
    Job(Job&& o) noexcept
        : session_(std::exchange(o.session_, nullptr)),
          computation_(std::exchange(o.computation_, 0)),
          inner_(std::move(o.inner_)) {
        o.inner_.reset();
    }
    Job& operator=(Job&& o) noexcept {
        if (this != &o) {
            session_ = std::exchange(o.session_, nullptr);
            computation_ = std::exchange(o.computation_, 0);
            inner_ = std::move(o.inner_);
            o.inner_.reset();
        }
        return *this;
    }

    S* session() const { return session_; }
    uint64_t computation() const { return computation_; }
    bool runnable() const { return inner_.has_value(); }

    // Hands the session-level job over; the Job is spent afterwards.
    typename S::job_t take() {
        expecting(inner_.has_value(), "frontend::Job: the job was already run");
        typename S::job_t j = std::move(*inner_);
        inner_.reset();
        return j;
    }

private:
    S* session_;
    uint64_t computation_;
    std::optional<typename S::job_t> inner_;
};

template <ComputationSession S, RecordValue RetV, RecordValue... ArgVs>
Job<S, RetV, ArgVs...> prepare(S& sess, const Computation<RetV, ArgVs...>& comp) {
    return Job<S, RetV, ArgVs...>(&sess, comp.id(), sess.prepare_computation(comp.artifact()));
}

// ---------------------------------------------------------------------------
// Result: the outputs this party receives.
// ---------------------------------------------------------------------------
template <RecordValue RetV>
class Result {
public:
    Result(uint64_t computation, std::optional<typename RetV::clear_t> value)
        : computation_(computation), value_(std::move(value)) {}

    // Present where this party receives the output slot.
    std::optional<typename RetV::clear_t> operator[](const OutputHandle<RetV>& h) const {
        expecting(h.computation() == computation_,
                  "frontend::Result: the handle belongs to another computation");
        return value_;
    }

private:
    uint64_t computation_;
    std::optional<typename RetV::clear_t> value_;
};

namespace detail {
template <class B>
void add_binding(session::RawBindings& raw, uint64_t computation, const B& b) {
    static_assert(is_binding_v<std::remove_cvref_t<B>>,
        "frontend::run: pass input values as cf::bind(handle, value)");
    expecting(b.computation == computation,
              "frontend::run: a binding names a handle of another computation");
    expecting(b.slot < raw.inputs.size(), "frontend::run: a binding names a slot out of range");
    expecting(!raw.inputs[b.slot].has_value(), "frontend::run: an input slot is bound twice");
    raw.inputs[b.slot].emplace(b.bits.begin(), b.bits.end());
}
}  // namespace detail

// cf::run(sess, std::move(job), cf::bind(handle, value)...): this party's Party
// inputs and every Public input. The bindings are forwarding references so that
// this overload, not the live run(body, args...), takes a job.
template <ComputationSession S, RecordValue RetV, RecordValue... ArgVs, class... Bs>
Result<RetV> run(S& sess, Job<S, RetV, ArgVs...>&& job, Bs&&... binds) {
    expecting(job.session() == &sess, "frontend::run: the job was prepared on another session");
    expecting(job.runnable(), "frontend::run: the job was already run");
    const uint64_t id = job.computation();
    session::RawBindings raw;
    raw.inputs.resize(sizeof...(ArgVs));
    (detail::add_binding(raw, id, binds), ...);
    session::RawResult r = sess.run_computation(job.take(), raw);
    expecting(r.outputs.size() == 1, "frontend::run: the session returned the wrong number of output slots");
    if (!r.outputs[0].has_value()) return Result<RetV>(id, std::nullopt);
    expecting(r.outputs[0]->size() == (std::size_t)RetV::width(),
              "frontend::run: the session returned an output of the wrong width");
    std::array<bool, (std::size_t)RetV::width()> bits{};
    for (std::size_t k = 0; k < bits.size(); ++k) bits[k] = (*r.outputs[0])[k] != 0;
    return Result<RetV>(id, RetV::decode(bits.data()));
}

}  // namespace frontend
}  // namespace emp
#endif  // EMP_FRONTEND_COMPUTATION_FN_H__
