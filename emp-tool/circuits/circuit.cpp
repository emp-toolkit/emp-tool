// Single TU that pays the cost of instantiating the `block`-typed circuit
// classes. Every other TU sees the matching `extern template class …;`
// declarations in circuit.h and skips re-instantiation.
//
// Member function TEMPLATES (e.g. Bit_T::reveal<O>) are not covered here —
// they're implicitly instantiated per call site as before. Adding common
// reveal<bool>/<uint32_t>/<uint64_t>/<std::string> instantiations here
// would extend the savings if profiling shows it's worth it.

#include "emp-tool/circuits/circuit.h"

namespace emp {

template class Bit_T<block>;
template class BitVec_T<block>;
template class UnsignedInt_T<block, 0>;
template class UnsignedInt_T<block, 8>;
template class UnsignedInt_T<block, 16>;
template class UnsignedInt_T<block, 32>;
template class UnsignedInt_T<block, 64>;
template class SignedInt_T<block, 0>;
template class SignedInt_T<block, 8>;
template class SignedInt_T<block, 16>;
template class SignedInt_T<block, 32>;
template class SignedInt_T<block, 64>;
template class Float_T<block>;
template class AES_Calculator_T<block>;
template class AES_128_CTR_Calculator_T<block>;
template class SHA3_256_Calculator_T<block>;

}  // namespace emp
