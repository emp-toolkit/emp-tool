#ifndef EMP_UTILS_FIELD_UTILS_H__
#define EMP_UTILS_FIELD_UTILS_H__

#if defined(__x86_64__) && defined(__BMI2__)
inline uint64_t mul64(uint64_t a, uint64_t b, uint64_t *c) {
  return _mulx_u64((unsigned long long)a, (unsigned long long)b,
                   (unsigned long long *)c);
}
//
#else
inline uint64_t mul64(uint64_t a, uint64_t b, uint64_t *c) {
  __uint128_t aa = a;
  __uint128_t bb = b;
  auto cc = aa * bb;
  *c = cc >> 64;
  return (uint64_t)cc;
}
#endif

#endif // EMP_UTILS_FIELD_UTILS_H__
