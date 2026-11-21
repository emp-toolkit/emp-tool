#ifndef EMP_IR_SESSION_COMPUTATION_SESSION_H__
#define EMP_IR_SESSION_COMPUTATION_SESSION_H__

// Circuit mode: a session runs a ComputationArtifact (ir/computation.h) in two
// calls.
//
//   auto job = sess.prepare_computation(artifact);          // offline: every role is known
//   session::RawResult r = sess.run_computation(std::move(job), bindings);   // online
//
// prepare_computation validates the computation before any traffic and does the
// session's offline work. run_computation consumes the job, checks the
// bindings before any traffic, and returns, for every output slot, the bits
// when this party receives the slot and nothing otherwise. A job is
// single-use; a session documents how many jobs may be outstanding and what
// else it allows meanwhile. The typed surface over this raw one is
// circuits/frontend/computation_fn.h.
//
// Bindings: a party binds exactly the Party slots it owns plus every Public
// slot, each with exactly its slot's width (check_bindings). A session that
// holds every party in one process, whose party() is PUBLIC, binds every slot.
// Public bits are not compared between parties at run time.

#include "emp-tool/ir/computation.h"
#include "emp-tool/ir/session/session.h"      // Session
#include "emp-tool/runtime/core/constants.h"  // PUBLIC
#include "emp-tool/runtime/core/utils.h"      // expecting
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace emp {
namespace session {

// One slot's bits as byte-bools (docs/api_conventions.md), in the slot's wire
// order: bit k of an input slot feeds its k-th wire, and bit k of an output slot
// is its k-th entry of program.outputs.
using Bits = std::vector<uint8_t>;

// Indexed by input slot; a slot this party does not bind stays empty.
struct RawBindings {
	std::vector<std::optional<Bits>> inputs;
};

// Indexed by output slot; a slot this party does not receive stays empty.
struct RawResult {
	std::vector<std::optional<Bits>> outputs;
};

// Whether `party` supplies input slot `r`: its owner, or anyone for Public.
// `party == PUBLIC` stands for every party at once.
inline bool binds(const circuit::InputRole& r, int party) {
	return party == PUBLIC || r.kind == circuit::InputKind::Public || r.owner == party;
}

// Whether `party` receives output slot `r`: its recipient, or anyone for Public.
inline bool receives(const circuit::OutputRole& r, int party) {
	return party == PUBLIC || r.kind == circuit::OutputKind::Public || r.recipient == party;
}

// The binding rule above, fatal on the first violation.
inline void check_bindings(const circuit::ComputationArtifact& a, int party,
                           const RawBindings& b) {
	expecting(b.inputs.size() == a.io.inputs.size(),
	          "check_bindings: the bindings do not have one entry per input slot");
	for (std::size_t i = 0; i < a.io.inputs.size(); ++i) {
		const circuit::InputRole& r = a.io.inputs[i];
		const auto where = [&] { return "check_bindings: input slot " + std::to_string(i); };
		if (!binds(r, party)) {
			expecting(!b.inputs[i].has_value(),
			          [&] { return where() + " belongs to another party"; });
			continue;
		}
		expecting(b.inputs[i].has_value(), [&] { return where() + " is unbound"; });
		expecting(b.inputs[i]->size() == (std::size_t)r.width,
		          [&] { return where() + " is bound with a bit count other than its width"; });
	}
}

}  // namespace session

// A session that runs computations in circuit mode (see the top of this file).
template <class S>
concept ComputationSession =
    Session<S> &&
    requires(S& s, std::shared_ptr<const circuit::ComputationArtifact> a,
             typename S::job_t j, const session::RawBindings& b) {
        { s.prepare_computation(a) } -> std::same_as<typename S::job_t>;
        { s.run_computation(std::move(j), b) } -> std::same_as<session::RawResult>;
    };

}  // namespace emp
#endif  // EMP_IR_SESSION_COMPUTATION_SESSION_H__
