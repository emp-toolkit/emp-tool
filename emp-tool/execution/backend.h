#ifndef EMP_BACKEND_H__
#define EMP_BACKEND_H__

#include "emp-tool/core/block.h"
#include "emp-tool/core/constants.h"
#include <cstddef>
#include <cstdint>

namespace emp {

// Single abstract entry point for everything an execution backend has to
// provide: gate evaluation (and / xor / not / public_label) plus input
// feeding and output revealing. Wires are passed through as void* so the
// dispatch interface stays one type regardless of the protocol's wire
// representation; each concrete backend interprets the pointers as its
// own wire_t. The user-facing `Bit_T<Wire>` (Phase 2) carries the static
// wire type so user code stays type-safe; only the dispatch boundary is
// type-erased.
//
// Backends whose feed / reveal need cryptographic machinery beyond what
// emp-tool ships (typically OT) leave the default no-op overrides in
// place and a downstream subclass (e.g. in emp-ot) supplies them.
class Backend {
public:
	int party;

	Backend(int party_ = PUBLIC) : party(party_) {}
	virtual ~Backend() = default;

	// Bytes per wire. Used by Bit_T<Wire>'s constructor to assert the
	// active backend matches the static wire type. Cheap virtual call,
	// happens once at backend setup, not per gate.
	virtual size_t wire_bytes() const = 0;

	// Gate dispatch — out-param style. Backends that need to support
	// arbitrarily large wires don't pay return-by-value costs.
	virtual void and_gate(void* out, const void* l, const void* r) = 0;
	virtual void xor_gate(void* out, const void* l, const void* r) = 0;
	virtual void not_gate(void* out, const void* in)               = 0;
	virtual void public_label(void* out, bool b)                   = 0;

	// Bulk variants: amortize the virtual dispatch over n contiguous
	// wires. Default implementation loops the scalar path; backends with
	// a faster bulk shape (e.g. SIMD XOR over a wide wire's bytes,
	// batched halfgate hashing) can override. Wires are laid out as a
	// flat byte array of n * wire_bytes(), no padding.
	virtual void and_gate_n(void* out, const void* l, const void* r, size_t n) {
		const size_t w = wire_bytes();
		auto* o = static_cast<unsigned char*>(out);
		auto* a = static_cast<const unsigned char*>(l);
		auto* b = static_cast<const unsigned char*>(r);
		for (size_t i = 0; i < n; ++i)
			and_gate(o + i * w, a + i * w, b + i * w);
	}
	virtual void xor_gate_n(void* out, const void* l, const void* r, size_t n) {
		const size_t w = wire_bytes();
		auto* o = static_cast<unsigned char*>(out);
		auto* a = static_cast<const unsigned char*>(l);
		auto* b = static_cast<const unsigned char*>(r);
		for (size_t i = 0; i < n; ++i)
			xor_gate(o + i * w, a + i * w, b + i * w);
	}
	virtual void not_gate_n(void* out, const void* in, size_t n) {
		const size_t w = wire_bytes();
		auto* o = static_cast<unsigned char*>(out);
		auto* a = static_cast<const unsigned char*>(in);
		for (size_t i = 0; i < n; ++i)
			not_gate(o + i * w, a + i * w);
	}

	// Input feeding. `from_party` is the party providing the bits;
	// PUBLIC means everyone agrees on the values.
	virtual void feed(void* /*out*/, int /*from_party*/,
	                  const bool* /*in*/, size_t /*n*/) {}

	// Output revealing. `to_party` is the party receiving the cleartext
	// outputs; PUBLIC means everyone gets them.
	virtual void reveal(bool* /*out*/, int /*to_party*/,
	                    const void* /*in*/, size_t /*n*/) {}

	// Total AND gates evaluated so far. Returns 0 for backends that
	// don't track this.
	virtual uint64_t num_and() { return 0; }

	// Hook for backends that need to flush / finalize at end of circuit
	// (file dumps, network drains, etc.).
	virtual void finalize() {}
};

// The single global backend pointer. Set by the protocol's setup helper
// (e.g. setup_clear_backend), used by every Bit / Integer / circuit op.
// Honors the THREADING build flag for thread-local backends.
#ifndef THREADING
extern Backend* backend;
#else
extern __thread Backend* backend;
#endif

enum RTCktOpt { on, off };

}  // namespace emp
#endif
