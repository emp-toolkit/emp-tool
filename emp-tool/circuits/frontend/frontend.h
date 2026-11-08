#ifndef EMP_FRONTEND_H__
#define EMP_FRONTEND_H__

// Umbrella for the circuit-function frontend over the C++20 BooleanContext model:
// write a PURE circuit body once, compile() it into a context-free Circuit<Sig>,
// and run() it on ANY context (plaintext / garbled 2PC / ZK / ...), exactly like
// the built-in .empbc circuits. Typed circuit values (Bit_T/UInt_T/Int_T/
// Float_T<Ctx>) come from circuits/typed.h; compile() names them over RecordCtx
// via the rec:: aliases in frontend/rec.h.
//
// The context is passed explicitly; there is no global backend. This is the
// frontend protocol libraries build on.

#include "emp-tool/circuits/frontend/circuit_fn.h"
#include "emp-tool/circuits/frontend/rec.h"

#endif  // EMP_FRONTEND_H__
