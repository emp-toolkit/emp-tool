#ifndef EMP_UTILS_FIELD_FP61_H__
#define EMP_UTILS_FIELD_FP61_H__

#include "emp-tool/utils/block.h"
#include "emp-tool/utils/fields/utils.h"
#include <cstdint>
namespace emp {

// finite field Fp, p = 2^61 - 1
class FP61 {
public:
  static uint64_t PR_bit_len;
  static uint64_t PR;
  static block DoublePR;
  static uint64_t Low61bMask;
  static block DoubleLow61bMask;

  uint64_t val;

  FP61() { val = 0; }

  FP61(uint64_t input, bool mod_it = true) {
    if (mod_it)
      val = mod(input);
    else
      val = input;
  }

  void operator=(const uint64_t rhs) { this->val = mod(rhs); }

  void operator=(const FP61 rhs) { this->val = rhs.val; }

  bool operator==(const uint64_t rhs) {
    return (this->val == mod(rhs)) ? true : false;
  }

  bool operator==(const FP61 rhs) {
    return (this->val == rhs.val) ? true : false;
  }

  bool operator!=(const uint64_t rhs) {
    return (this->val == mod(rhs)) ? false : true;
  }

  bool operator!=(const FP61 rhs) {
    return (this->val == rhs.val) ? false : true;
  }

  FP61 operator+(const FP61 rhs) {
    FP61 res(*this);
    res.val = add_mod(res.val, rhs.val);
    return res;
  }

  FP61 operator*(const FP61 rhs) {
    FP61 res(*this);
    res.val = mult_mod(res.val, rhs.val);
    return res;
  }

  uint64_t mod(uint64_t x) {
    uint64_t i = (x & PR) + (x >> PR_bit_len);
    return (i >= PR) ? i - PR : i;
  }

  // c = val + a mod PR
  uint64_t add_mod(uint64_t a, uint64_t b) {
    uint64_t res = a + b;
    return (res >= PR) ? (res - PR) : res;
  }

  // c = a * b mod PR
  uint64_t mult_mod(uint64_t a, uint64_t b) {
    uint64_t c = 0;
    uint64_t e = mul64(a, b, (uint64_t *)&c);
    uint64_t res = (e & PR) + ((e >> PR_bit_len) ^ (c << (64 - PR_bit_len)));
    return (res >= PR) ? (res - PR) : res;
  }

  // c = (a1 * b || a2 * b) mod PR
  static block mult_mod(block a, uint64_t b) {
    uint64_t H = _mm_extract_epi64(a, 1);
    uint64_t L = _mm_extract_epi64(a, 0);
    block bs[2];
    uint64_t *is = (uint64_t *)(bs);
    is[1] = mul64(H, b, (uint64_t *)(is + 3));
    is[0] = mul64(L, b, (uint64_t *)(is + 2));
    block t1 = bs[0] & DoublePR;
    block t2 = _mm_srli_epi64(bs[0], PR_bit_len) ^
               _mm_slli_epi64(bs[1], 64 - PR_bit_len);
    block res = _mm_add_epi64(t1, t2);
    return vec_partial_mod(res);
  }

  // c = (a1 + b || a2 + b) mod PR
  static block add_mod(block a, uint64_t b) {
    block res = _mm_add_epi64(a, _mm_set_epi64((__m64)b, (__m64)b));
    return vec_partial_mod(res);
  }

  // c = (a1 + b1 || a2 + b2) mod PR
  static block add_mod(block a, block b) {
    block res = _mm_add_epi64(a, b);
    return vec_partial_mod(res);
  }

  // c = (a1 || a2) mod PR
  static block vec_mod(block i) {
    i = _mm_add_epi64((i & DoublePR), _mm_srli_epi64(i, PR_bit_len));
    return vec_partial_mod(i);
  }

  // (c1 || c2) = (a1 || a2) (partial) mod PR
  static block vec_partial_mod(block i) {
    return _mm_sub_epi64(
        i, _mm_andnot_si128(_mm_cmpgt_epi64(DoublePR, i), DoublePR));
  }
};

uint64_t FP61::PR_bit_len = 61;
uint64_t FP61::PR = 2305843009213693951;
block FP61::DoublePR = makeBlock(2305843009213693951, 2305843009213693951);
uint64_t FP61::Low61bMask = (1ULL << 61) - 1;
block FP61::DoubleLow61bMask = makeBlock((1ULL << 61) - 1, (1ULL << 61) - 1);

} // namespace emp
#endif // EMP_UTILS_FIELD_FP61_H__
