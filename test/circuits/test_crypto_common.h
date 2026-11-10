#ifndef EMP_TEST_CRYPTO_COMMON_H__
#define EMP_TEST_CRYPTO_COMMON_H__
// Helpers to drive a context-generic crypto kernel over the three analysis
// contexts: ClearCtx (concrete bits), RecordCtx (record a BooleanProgram, then
// replay it on ClearCtx), and CountCtx (gate counts). A "driver" is a generic
// callable drv(ctx, const Wire* in, Wire* out) that builds typed values from the
// input wires, runs the kernel, and writes output wires.
#include "emp-tool/ir/context/context.h"
#include "emp-tool/ir/session/clear_session.h"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace test_crypto {

// LSB-first within byte, byte-sequential (the emp-tool circuit convention).
inline std::vector<uint8_t> bits_from_hex(const std::string& hex) {
  auto hv = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
  std::vector<uint8_t> b;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    int byte = hv(hex[i]) * 16 + hv(hex[i + 1]);
    for (int k = 0; k < 8; ++k) b.push_back((uint8_t)((byte >> k) & 1));
  }
  return b;
}
inline std::string hex_from_bits(const std::vector<uint8_t>& bits) {
  static const char* H = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i + 7 < bits.size(); i += 8) {
    int byte = 0;
    for (int k = 0; k < 8; ++k) byte |= (bits[i + k] & 1) << k;
    s += H[byte >> 4]; s += H[byte & 15];
  }
  return s;
}

// Run the driver on ClearCtx over concrete input bits; return the output bits.
template <class Drv>
inline std::vector<uint8_t> clear_run(Drv drv, const std::vector<uint8_t>& in_bits, int n_out) {
  emp::ClearSession::ctx_t ctx;
  std::vector<uint8_t> in(in_bits), out((size_t)n_out, 0);
  drv(ctx, in.data(), out.data());
  for (auto& x : out) x &= 1;
  return out;
}

// Record the driver into a BooleanProgram (all external inputs reserved before any
// gate), then replay it on ClearCtx over the same input bits.
template <class Drv>
inline std::vector<uint8_t> record_replay(Drv drv, const std::vector<uint8_t>& in_bits, int n_out) {
  const int n_in = (int)in_bits.size();
  emp::RecordCtx rc;
  std::vector<uint32_t> in((size_t)n_in), out((size_t)n_out);
  if (n_in > 0) {
    uint32_t base = rc.external_input(n_in);   // reserve ALL inputs first
    for (int i = 0; i < n_in; ++i) in[i] = base + (uint32_t)i;
  }
  drv(rc, in.data(), out.data());
  emp::circuit::BooleanProgram prog =
      rc.finish(std::span<const uint32_t>(out.data(), (size_t)n_out));

  emp::ClearSession::ctx_t cx;
  std::vector<uint8_t> cin(in_bits);
  std::vector<uint8_t> cow = emp::execute_program(
      cx, prog, std::span<const uint8_t>(cin.data(), cin.size()));
  std::vector<uint8_t> r((size_t)n_out);
  for (int i = 0; i < n_out; ++i) r[i] = cow[(size_t)i] & 1;
  return r;
}

// AND-gate count of the driver via CountCtx.
template <class Drv>
inline uint64_t count_ands(Drv drv, int n_in, int n_out) {
  emp::CountCtx ctx;
  std::vector<uint8_t> in((size_t)n_in, 0), out((size_t)n_out, 0);
  drv(ctx, in.data(), out.data());
  return ctx.ands;
}

}  // namespace test_crypto
#endif  // EMP_TEST_CRYPTO_COMMON_H__
