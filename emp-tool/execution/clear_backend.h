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

// Plaintext / circuit-printer backend. A wire (still `block`-shaped here so
// existing emp-tool tests are unaffected) carries:
//   - low 64 bits: a sentinel tag {P0, P1, S0, S1} encoding (public|private,
//     value), and
//   - high 64 bits: a gate id used when printing the circuit to file.
//
// When constructed with a non-empty filename, ClearBackend writes a circuit
// in Bristol-fashion-ish format on `finalize()`; otherwise it just evaluates
// in plaintext.
class ClearBackend : public Backend {
public:
	static constexpr uint64_t P1 = 1;
	static constexpr uint64_t P0 = 2;
	static constexpr uint64_t S0 = 3;
	static constexpr uint64_t S1 = 4;

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

	block public_one_blk, public_zero_blk;

	explicit ClearBackend(const std::string& filename_ = "")
	    : Backend(PUBLIC), filename(filename_) {
		public_one_blk = zero_block;
		public_zero_blk = zero_block;
		auto* a = reinterpret_cast<uint64_t*>(&public_one_blk);
		a[0] = P1;
		a = reinterpret_cast<uint64_t*>(&public_zero_blk);
		a[0] = P0;

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

	size_t wire_bytes() const override { return sizeof(block); }

	void public_label(void* out, bool b) override {
		*static_cast<block*>(out) = b ? public_one_blk : public_zero_blk;
	}

	bool get_value(const block& w) const {
		auto* a = reinterpret_cast<const uint64_t*>(&w);
		return !(a[0] == S0 || a[0] == P0);
	}

	void and_gate(void* out, const void* l_, const void* r_) override {
		const block& l = *static_cast<const block*>(l_);
		const block& r = *static_cast<const block*>(r_);
		auto* la = reinterpret_cast<const uint64_t*>(&l);
		auto* ra = reinterpret_cast<const uint64_t*>(&r);

		if (la[0] == P1) { *static_cast<block*>(out) = r; return; }
		if (ra[0] == P1) { *static_cast<block*>(out) = l; return; }
		if (la[0] == P0 || ra[0] == P0) {
			*static_cast<block*>(out) = public_zero_blk;
			return;
		}
		block res = zero_block;
		auto* a = reinterpret_cast<uint64_t*>(&res);
		a[0] = (la[0] == S0 || ra[0] == S0) ? S0 : S1;
		a[1] = static_cast<uint64_t>(gid);
		if (print)
			fout << "2 1 " << la[1] << ' ' << ra[1] << ' ' << gid << " AND\n";
		++gid; ++gates; ++ands;
		*static_cast<block*>(out) = res;
	}

	void xor_gate(void* out, const void* l_, const void* r_) override {
		const block& l = *static_cast<const block*>(l_);
		const block& r = *static_cast<const block*>(r_);
		auto* la = reinterpret_cast<const uint64_t*>(&l);
		auto* ra = reinterpret_cast<const uint64_t*>(&r);

		if (la[0] == P1) { not_gate(out, r_); return; }
		if (ra[0] == P1) { not_gate(out, l_); return; }
		if (la[0] == P0) { *static_cast<block*>(out) = r; return; }
		if (ra[0] == P0) { *static_cast<block*>(out) = l; return; }
		block res = zero_block;
		auto* a = reinterpret_cast<uint64_t*>(&res);
		a[0] = (la[0] == ra[0]) ? S0 : S1;
		a[1] = static_cast<uint64_t>(gid);
		if (print)
			fout << "2 1 " << la[1] << ' ' << ra[1] << ' ' << gid << " XOR\n";
		++gid; ++gates;
		*static_cast<block*>(out) = res;
	}

	void not_gate(void* out, const void* in_) override {
		const block& in = *static_cast<const block*>(in_);
		auto* ia = reinterpret_cast<const uint64_t*>(&in);
		if (ia[0] == P1) { *static_cast<block*>(out) = public_zero_blk; return; }
		if (ia[0] == P0) { *static_cast<block*>(out) = public_one_blk;  return; }
		block res = zero_block;
		auto* a = reinterpret_cast<uint64_t*>(&res);
		a[0] = (ia[0] == S0) ? S1 : S0;
		a[1] = static_cast<uint64_t>(gid);
		if (print)
			fout << "1 1 " << ia[1] << ' ' << gid << " INV\n";
		++gid; ++gates;
		*static_cast<block*>(out) = res;
	}

	void feed(void* out, int from_party, const bool* in, size_t n) override {
		auto* lbls = static_cast<block*>(out);
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
		const block* lbls = static_cast<const block*>(in_);
		for (size_t i = 0; i < n; ++i) {
			auto* a = reinterpret_cast<const uint64_t*>(&lbls[i]);
			bool val = get_value(lbls[i]);
			bool is_pub = (a[0] == P0 || a[0] == P1);
			output_indices.push_back({is_pub, val,
			                          is_pub ? int64_t{0}
			                                 : static_cast<int64_t>(a[1])});
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
	block private_label(bool v) {
		block res = zero_block;
		auto* a = reinterpret_cast<uint64_t*>(&res);
		a[0] = v ? S1 : S0;
		a[1] = static_cast<uint64_t>(gid++);
		return res;
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
