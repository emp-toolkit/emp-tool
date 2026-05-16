#ifndef EMP_CLEAR_BACKEND_H__
#define EMP_CLEAR_BACKEND_H__

#include "emp-tool/core/block.h"
#include "emp-tool/core/utils.h"
#include "emp-tool/execution/backend.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace emp {

// Plaintext / circuit-printer backend. A wire carries:
//   - gid:        Bristol gate id (only meaningful for private wires)
//   - is_public:  1 = public constant, 0 = private share
//   - value:      0 or 1
// With a non-empty filename, ClearBackend writes the circuit in Bristol-
// fashion-ish format on `finalize()`; otherwise it just evaluates in
// plaintext.
struct ClearWire {
	uint64_t gid;
	uint32_t is_public;
	uint32_t value;
};

class ClearBackend : public Backend {
public:
	int64_t gid = 0;
	uint64_t gates = 0, ands = 0;
	uint64_t n1 = 0, n2 = 0, n3 = 0;
	bool print = false;
	std::string filename;
	std::ofstream fout;
	struct OutputRef {
		bool is_public;
		bool value;
		int64_t gate_id;
	};
	std::vector<OutputRef> output_indices;

	static constexpr ClearWire public_one  { /*gid=*/0, /*is_public=*/1, /*value=*/1 };
	static constexpr ClearWire public_zero { /*gid=*/0, /*is_public=*/1, /*value=*/0 };

	explicit ClearBackend(const std::string& filename_ = "")
	    : Backend(PUBLIC), filename(filename_) {
		print = !filename.empty();
		if (print) {
			fout.open(filename);
			// placeholder for header (rewritten in finalize)
			for (int i = 0; i < 200; ++i)
				fout << " ";
			fout << '\n';
		}
	}

	~ClearBackend() override {
		if (fout.is_open()) fout.close();
	}

	size_t wire_bytes() const override { return sizeof(ClearWire); }

	void public_label(void* out, bool b) override {
		*static_cast<ClearWire*>(out) = b ? public_one : public_zero;
	}

	void and_gate(void* out, const void* l_, const void* r_) override {
		const ClearWire& l = *static_cast<const ClearWire*>(l_);
		const ClearWire& r = *static_cast<const ClearWire*>(r_);
		ClearWire& o = *static_cast<ClearWire*>(out);

		if (l.is_public) { o = l.value ? r : public_zero; return; }
		if (r.is_public) { o = r.value ? l : public_zero; return; }
		o = ClearWire{ static_cast<uint64_t>(gid), 0, l.value & r.value };
		if (print)
			fout << "2 1 " << l.gid << ' ' << r.gid << ' ' << gid << " AND\n";
		++gid; ++gates; ++ands;
	}

	void xor_gate(void* out, const void* l_, const void* r_) override {
		const ClearWire& l = *static_cast<const ClearWire*>(l_);
		const ClearWire& r = *static_cast<const ClearWire*>(r_);
		ClearWire& o = *static_cast<ClearWire*>(out);

		if (l.is_public && l.value) { not_gate(out, r_); return; }
		if (r.is_public && r.value) { not_gate(out, l_); return; }
		if (l.is_public)            { o = r; return; }
		if (r.is_public)            { o = l; return; }
		o = ClearWire{ static_cast<uint64_t>(gid), 0, l.value ^ r.value };
		if (print)
			fout << "2 1 " << l.gid << ' ' << r.gid << ' ' << gid << " XOR\n";
		++gid; ++gates;
	}

	void not_gate(void* out, const void* in_) override {
		const ClearWire& in = *static_cast<const ClearWire*>(in_);
		ClearWire& o = *static_cast<ClearWire*>(out);

		if (in.is_public) { o = in.value ? public_zero : public_one; return; }
		o = ClearWire{ static_cast<uint64_t>(gid), 0, in.value ^ 1u };
		if (print)
			fout << "1 1 " << in.gid << ' ' << gid << " INV\n";
		++gid; ++gates;
	}

	void feed(void* out, int from_party, const bool* in, size_t n) override {
		auto* lbls = static_cast<ClearWire*>(out);
		if (from_party == PUBLIC) {
			for (size_t i = 0; i < n; ++i)
				public_label(&lbls[i], in[i]);
			return;
		}
		for (size_t i = 0; i < n; ++i)
			lbls[i] = private_label(in[i]);
		if (from_party == ALICE) n1 += n;
		else                     n2 += n;
	}

	void reveal(bool* out, int /*to_party*/, const void* in_, size_t n) override {
		const ClearWire* lbls = static_cast<const ClearWire*>(in_);
		for (size_t i = 0; i < n; ++i) {
			const bool is_pub = (lbls[i].is_public != 0);
			const bool val    = (lbls[i].value != 0);
			output_indices.push_back({is_pub, val,
			                          is_pub ? int64_t{0}
			                                 : static_cast<int64_t>(lbls[i].gid)});
			out[i] = val;
		}
		n3 += n;
	}

	uint64_t num_and() override { return ands; }

	void finalize() override {
		if (!print) return;
		// Trailer: synthesize a constant-0 wire (z_index = wires[0] XOR
		// wires[0]) and, if any public-true output needs it, a
		// constant-1 wire (o_index = NOT z_index). Both helpers must be
		// emitted before the output XOR block so the last n3 wires
		// remain exactly the output gates, as BristolFormat::compute
		// assumes (out[i] = wires[num_wire - n3 + i]). Each output XOR
		// routes through one of {z_index, o_index, real gate id} so
		// public-constant reveals carry the right value instead of
		// leaking input wire 0.
		int64_t z_index = gid++;
		fout << "2 1 0 0 " << z_index << " XOR\n";
		size_t extra = 1;
		bool need_one = false;
		for (auto& v : output_indices)
			if (v.is_public && v.value) { need_one = true; break; }
		int64_t o_index = -1;
		if (need_one) {
			o_index = gid++;
			fout << "1 1 " << z_index << ' ' << o_index << " INV\n";
			++extra;
		}
		for (auto& v : output_indices) {
			int64_t src = v.is_public ? (v.value ? o_index : z_index)
			                          : v.gate_id;
			fout << "2 1 " << z_index << ' ' << src << ' ' << gid++ << " XOR\n";
		}
		gates += (extra + output_indices.size());

		fout.flush();
		fout.close();

		// Rewrite the placeholder header.
		std::fstream f(filename, std::fstream::in | std::fstream::out);
		f << gates << ' ' << gid << '\n';
		f << n1 << ' ' << n2 << ' ' << n3 << '\n';
		f.close();
	}

private:
	ClearWire private_label(bool v) {
		return ClearWire{ static_cast<uint64_t>(gid++), 0, v ? 1u : 0u };
	}
};

inline void setup_clear_backend(const std::string& filename = "") {
	backend = new ClearBackend(filename);
}

inline void finalize_clear_backend() {
	if (!backend) return;
	backend->finalize();
	delete backend;
	backend = nullptr;
}

}  // namespace emp
#endif
