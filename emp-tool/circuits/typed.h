#ifndef EMP_CIRCUIT_TYPED_H__
#define EMP_CIRCUIT_TYPED_H__

// Umbrella for the BooleanContext value layer: Bit_T / UInt_T / Int_T / Float_T /
// BitVec_T plus the two dynamic integer siblings. Each type lives in its own
// header; include this to get them all, or include just the one you need.

#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/circuits/signed_int.h"
#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/float.h"

#endif  // EMP_CIRCUIT_TYPED_H__
