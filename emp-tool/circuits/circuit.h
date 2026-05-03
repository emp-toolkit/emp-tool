#ifndef EMP_CIRCUIT_H__
#define EMP_CIRCUIT_H__

// Umbrella header for emp-tool/circuits/*. Includes all circuit primitives
// and exposes the `block`-typed default aliases (Bit, BitVec, UnsignedInt,
// SignedInt, Float, UInt{8,16,32,64}, Int{8,16,32,64}, AES_Calculator,
// AES_128_CTR_Calculator, SHA3_256_Calculator).
//
// Also declares `extern template` for the block instantiations so that each
// translation unit including this header skips the per-TU implicit
// instantiation of every method body. The actual instantiations live in
// emp-tool/circuits/circuit.cpp — exactly one object file pays the cost,
// every other TU just links to the symbols. This is purely a build-time
// optimisation; runtime behaviour is identical.
//
// Code that wants a non-`block` wire type instantiates Bit_T<W> /
// BitVec_T<W> / UnsignedInt_T<W,N> / etc. directly and pays its own
// implicit-instantiation cost.

#include "emp-tool/core/block.h"
#include "emp-tool/circuits/sortable.h"
#include "emp-tool/circuits/bit.h"
#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/circuits/signed_int.h"
#include "emp-tool/circuits/float32.h"
#include "emp-tool/circuits/circuit_file.h"
#include "emp-tool/circuits/aes_circuit.h"
#include "emp-tool/circuits/aes_128_ctr.h"
#include "emp-tool/circuits/sha3_256.h"

namespace emp {

// --- Default block-typed aliases ---
using Bit         = Bit_T<block>;
using BitVec      = BitVec_T<block>;
using UnsignedInt = UnsignedInt_T<block>;
using SignedInt   = SignedInt_T<block>;
using Float       = Float_T<block>;
using AES_Calculator         = AES_Calculator_T<block>;
using AES_128_CTR_Calculator = AES_128_CTR_Calculator_T<block>;
using SHA3_256_Calculator    = SHA3_256_Calculator_T<block>;

// Fixed-width integer aliases.
using UInt8  = UInt8_T<block>;
using UInt16 = UInt16_T<block>;
using UInt32 = UInt32_T<block>;
using UInt64 = UInt64_T<block>;
using Int8   = Int8_T<block>;
using Int16  = Int16_T<block>;
using Int32  = Int32_T<block>;
using Int64  = Int64_T<block>;

// --- Explicit instantiation declarations ---
// Suppresses implicit instantiation of these classes' member functions in
// every TU that includes this header. The matching `template class …;`
// definitions live in circuit.cpp.
extern template class Bit_T<block>;
extern template class BitVec_T<block>;
extern template class UnsignedInt_T<block, 0>;
extern template class UnsignedInt_T<block, 8>;
extern template class UnsignedInt_T<block, 16>;
extern template class UnsignedInt_T<block, 32>;
extern template class UnsignedInt_T<block, 64>;
extern template class SignedInt_T<block, 0>;
extern template class SignedInt_T<block, 8>;
extern template class SignedInt_T<block, 16>;
extern template class SignedInt_T<block, 32>;
extern template class SignedInt_T<block, 64>;
extern template class Float_T<block>;
extern template class AES_Calculator_T<block>;
extern template class AES_128_CTR_Calculator_T<block>;
extern template class SHA3_256_Calculator_T<block>;

}  // namespace emp
#endif  // EMP_CIRCUIT_H__
