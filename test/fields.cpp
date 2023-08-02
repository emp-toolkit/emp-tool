#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;
using namespace emp;

template <typename T, typename S> void add_mod(T a, T b) {
  S x(a);
  S y(b);
  S z = x + y;
  T c = (a + b) % S::PR;
  S zc(c);
  if (z != c) {
    error("addition error\n");
  }
  if (z != zc) {
    error("addition error\n");
  }
}

template <typename T, typename S> void add_mod_bin(T a, T b) {
  S x(a);
  S y(b);
  S z = x + y;
  T c = a ^ b;
  S zc(c);
  if (z != c) {
    error("addition error\n");
  }
  if (z != zc) {
    error("addition error\n");
  }
}

void mult_mod_59(uint64_t a, uint64_t b) {
  FP59 x(a);
  FP59 y(b);
  FP59 z = x * y;
  __uint128_t c = ((__uint128_t)a * (__uint128_t)b) % FP59::PR;
  FP59 zc((uint64_t)c);
  if (z != zc) {
    error("multiplication error\n");
  }
}

void mult_mod_61(uint64_t a, uint64_t b) {
  FP61 x(a);
  FP61 y(b);
  FP61 z = x * y;
  __uint128_t c = ((__uint128_t)a * (__uint128_t)b) % FP61::PR;
  FP61 zc((uint64_t)c);
  if (z != zc) {
    error("multiplication error\n");
  }
}

void mult_mod_128(block a, block b) {
  EFP2R128 x(a);
  EFP2R128 y(b);
  EFP2R128 z = x * y;
  block c;
  gfmul(a, b, &c);
  EFP2R128 zc(c);
  if (z != zc) {
    error("multiplication error\n");
  }
}

static std::size_t test_n = 10000;

void test_fp59() {
  PRG prg;
  uint64_t a[test_n];
  prg.random_data(a, test_n * sizeof(uint64_t));
  for (std::size_t i = 0; i < test_n; ++i) {
    a[i] = a[i] & FP59::Low59bMask;
    if (a[i] >= FP59::PR)
      a[i] -= FP59::PR;
  }
  for (std::size_t i = 0; i < test_n / 2; ++i) {
    add_mod<uint64_t, FP59>(a[2 * i], a[2 * i + 1]);
    mult_mod_59(a[2 * i], a[2 * i + 1]);
  }
}

void test_fp61() {
  PRG prg;
  uint64_t a[test_n];
  prg.random_data(a, test_n * sizeof(uint64_t));
  for (std::size_t i = 0; i < test_n; ++i) {
    a[i] = a[i] & FP61::Low61bMask;
    if (a[i] >= FP61::PR)
      a[i] -= FP61::PR;
  }
  for (std::size_t i = 0; i < test_n / 2; ++i) {
    add_mod<uint64_t, FP61>(a[2 * i], a[2 * i + 1]);
    mult_mod_61(a[2 * i], a[2 * i + 1]);
  }
}

void test_efp2r128() {
  PRG prg;
  block a[test_n];
  prg.random_data(a, test_n * sizeof(block));
  for (std::size_t i = 0; i < test_n / 2; ++i) {
    add_mod_bin<block, EFP2R128>(a[2 * i], a[2 * i + 1]);
    mult_mod_128(a[2 * i], a[2 * i + 1]);
  }
}

int main(void) {
  test_fp59();
  test_fp61();
  test_efp2r128();

  return 0;
}
