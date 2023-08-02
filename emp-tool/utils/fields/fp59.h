#ifndef EMP_UTILS_FIELD_FP59_H__
#define EMP_UTILS_FIELD_FP59_H__

#include "emp-tool/utils/block.h"
#include "emp-tool/utils/fields/utils.h"
#include <cstdint>
namespace emp {

// finite field Fp, p = 2^59 - 2^28 + 1
class FP59 {
public:
  static uint64_t PR_bit_len;
  static uint64_t PR;
  static block DoublePR;
  static uint64_t Low59bMask;
  static block DoubleLow59bMask;

  uint64_t val;

  FP59() { val = 0; }

  FP59(uint64_t input, bool mod_it = true) {
    if (mod_it)
      val = mod(input);
    else
      val = input;
  }

  void operator=(const uint64_t rhs) { this->val = mod(rhs); }

  void operator=(const FP59 rhs) { this->val = rhs.val; }

  bool operator==(const uint64_t rhs) {
    return (this->val == mod(rhs)) ? true : false;
  }

  bool operator==(const FP59 rhs) {
    return (this->val == rhs.val) ? true : false;
  }

  bool operator!=(const uint64_t rhs) {
    return (this->val == mod(rhs)) ? false : true;
  }

  bool operator!=(const FP59 rhs) {
    return (this->val == rhs.val) ? false : true;
  }

  FP59 operator+(const FP59 rhs) {
    FP59 res(*this);
    res.val = add_mod(res.val, rhs.val);
    return res;
  }

  FP59 operator*(const FP59 rhs) {
    FP59 res(*this);
    res.val = mult_mod(res.val, rhs.val);
    return res;
  }

  uint64_t mod(uint64_t x) {
    uint64_t i = x >> 59;
    i = (i << 28) - i + (x & Low59bMask);
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
    uint64_t i = (c << 5) + (e >> 59);

    uint64_t j = i >> 31;
    j = (j << 28) - j + ((i << 28) & Low59bMask);
    j = (j >= PR) ? j - PR : j;

    i = j + (e & Low59bMask) + PR - i;
    i = (i >= PR) ? (i - PR) : i;
    return (i >= PR) ? (i - PR) : i;
  }

  // c = (a1 * b || a2 * b) mod PR
  static block mult_mod(block a, uint64_t b) {
    uint64_t H = _mm_extract_epi64(a, 1);
    uint64_t L = _mm_extract_epi64(a, 0);
    block bs[2];
    uint64_t *is = (uint64_t *)(bs);
    is[1] = mul64(H, b, (uint64_t *)(is + 3));
    is[0] = mul64(L, b, (uint64_t *)(is + 2));
    block t1 =
        _mm_add_epi64(_mm_slli_epi64(bs[1], 5), _mm_srli_epi64(bs[0], 59));

    block t2 = _mm_srli_epi64(t1, 31);
    block t3 = _mm_slli_epi64(t2, 28);
    t3 = _mm_sub_epi64(t3, t2);
    t3 = _mm_add_epi64(t3, _mm_slli_epi64(t1, 28) & DoubleLow59bMask);
    t3 = _mm_sub_epi64(
        t3, _mm_andnot_si128(_mm_cmpgt_epi64(DoublePR, t3), DoublePR));

    t3 = _mm_add_epi64(t3, bs[0] & DoubleLow59bMask);
    t1 = _mm_sub_epi64(DoublePR, t1);
    t3 = _mm_add_epi64(t3, t1);
    t3 = _mm_sub_epi64(
        t3, _mm_andnot_si128(_mm_cmpgt_epi64(DoublePR, t3), DoublePR));
    return _mm_sub_epi64(
        t3, _mm_andnot_si128(_mm_cmpgt_epi64(DoublePR, t3), DoublePR));
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

  // c = (a1 + b1 || a2 + b2) mod PR
  static block vec_mod(block i) {
    block x = _mm_srli_epi64(i, 59);
    x = _mm_sub_epi64(_mm_slli_epi64(x, 28), x);
    x = _mm_add_epi64(x, i & DoubleLow59bMask);
    return vec_partial_mod(x);
  }

  // (c1 || c2) = (a1 || a2) (partial) mod PR
  static block vec_partial_mod(block i) {
    return _mm_sub_epi64(
        i, _mm_andnot_si128(_mm_cmpgt_epi64(DoublePR, i), DoublePR));
  }
};

uint64_t FP59::PR_bit_len = 59;
uint64_t FP59::PR = 576460752034988033;
block FP59::DoublePR = makeBlock(576460752034988033, 576460752034988033);
uint64_t FP59::Low59bMask = (1ULL << 59) - 1;
block FP59::DoubleLow59bMask = makeBlock((1ULL << 59) - 1, (1ULL << 59) - 1);

} // namespace emp
#endif // EMP_UTILS_FIELD_FP59_H__
