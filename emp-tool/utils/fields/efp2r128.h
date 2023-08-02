#ifndef EMP_UTILS_FIELD_EFP2R128_H__
#define EMP_UTILS_FIELD_EFP2R128_H__

#include "emp-tool/utils/block.h"
#include "emp-tool/utils/f2k.h"
#include "emp-tool/utils/fields/utils.h"
#include <cstdint>
namespace emp {

// extension field Fpr, p = 2, r = 128 
class EFP2R128 {
public:
  static uint64_t PR_bit_len;

  block val;

  EFP2R128() { val = zero_block; }

  EFP2R128(block input, bool mod_it = true) {
    val = input;
  }

  void operator=(const block rhs) { this->val = rhs; }

  void operator=(const EFP2R128 rhs) { this->val = rhs.val; }

  bool operator==(const block rhs) {
    block vcmp = _mm_xor_si128(this->val, rhs);
    if(_mm_testz_si128(vcmp, vcmp))
      return true;
    return false;
  }

  bool operator==(const EFP2R128 rhs) {
    block vcmp = _mm_xor_si128(this->val, rhs.val);
    if(_mm_testz_si128(vcmp, vcmp))
      return true;
    return false;
  }

  bool operator!=(const block rhs) {
    block vcmp = _mm_xor_si128(this->val, rhs);
    if(_mm_testz_si128(vcmp, vcmp))
      return false;
    return true;
  }

  bool operator!=(const EFP2R128 rhs) {
    block vcmp = _mm_xor_si128(this->val, rhs.val);
    if(_mm_testz_si128(vcmp, vcmp))
      return false;
    return true;
  }

  EFP2R128 operator+(const EFP2R128 rhs) {
    EFP2R128 res(*this);
    res.val = res.val ^ rhs.val;
    return res;
  }

  EFP2R128 operator*(const EFP2R128 rhs) {
    EFP2R128 res(*this);
    gfmul(this->val, rhs.val, &(res.val));
    return res;
  }

};

uint64_t EFP2R128::PR_bit_len = 61;

} // namespace emp
#endif // EMP_UTILS_FIELD_EFP2R128_H__
