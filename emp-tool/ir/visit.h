#ifndef EMP_IR_VISIT_H__
#define EMP_IR_VISIT_H__

// The shared gate-walk primitive: walk gates once, in order, dispatching each to
// the matching visitor method. Op dispatch is not unique to this file — it also
// lives in validate.h, schedule.h, and passes.h; the
// guarantee is that every such site is a defaultless switch and the build carries
// -Werror=switch, so adding an Op is a build error at each dispatch site, never a
// silent fallthrough. (validate.h additionally range-checks the raw op byte of
// loaded files — the one sanctioned out-of-range path.) A consumer with a
// different wire model (e.g. ag2pc, which replays the same program once per
// garbling phase against a swapped backend and threads wire IDs rather than
// values) calls for_each_gate directly; ir/execute.h is the value wrapper most
// callers want.

#include "emp-tool/ir/program.h"

namespace emp {
namespace circuit {

// Visitor contract (methods take wire ids; the visitor owns the wire storage):
//   void and_gate(uint32_t out, uint32_t in0, uint32_t in1);
//   void xor_gate(uint32_t out, uint32_t in0, uint32_t in1);
//   void not_gate(uint32_t out, uint32_t in0);
//   void const_gate(uint32_t out, bool value);   // value: Const1 -> true
// Adding a new Op forces a new case here AND a new method on every visitor.
template <class Visitor>
inline void for_each_gate(const BooleanProgram& p, Visitor&& v) {
	for (const Gate& g : p.gates) {
		switch (g.op) {
			case Op::And:    v.and_gate(g.out, g.in0, g.in1); break;
			case Op::Xor:    v.xor_gate(g.out, g.in0, g.in1); break;
			case Op::Not:    v.not_gate(g.out, g.in0);        break;
			case Op::Const0: v.const_gate(g.out, false);      break;
			case Op::Const1: v.const_gate(g.out, true);       break;
		}
	}
}

}  // namespace circuit
}  // namespace emp
#endif  // EMP_IR_VISIT_H__
