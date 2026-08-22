#ifndef EMP_CIRCUITS_CIRCUITS_H__
#define EMP_CIRCUITS_CIRCUITS_H__

// circuits — the concrete circuit value families (Bit_T / BitVec_T / UInt_T /
// Int_T / Float_T and dynamic integers), their uniform metadata accessor, the numeric
// kernels, sort, the in-circuit crypto primitives (AES-128 / SHA-256 / Keccak-f /
// SHA3-256), and the compile/run frontend. It builds on ir and runtime.

#include "emp-tool/circuits/typed.h"
#include "emp-tool/circuits/value_traits.h"
#include "emp-tool/circuits/numeric_kernels.h"
#include "emp-tool/circuits/sort.h"
#include "emp-tool/circuits/crypto/crypto.h"
#include "emp-tool/circuits/frontend/frontend.h"

#endif  // EMP_CIRCUITS_CIRCUITS_H__
