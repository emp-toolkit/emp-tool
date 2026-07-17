#ifndef EMP_IR_EMPBC_H__
#define EMP_IR_EMPBC_H__

// .empbc — the one native persisted circuit format for emp-tool. A flat header
// plus gate and output records. Indices are stored 16- or 32-bit on disk
// (writer auto-selects 16 when num_wires fits), but ALWAYS decode into a single
// runtime BooleanProgram with uint32_t indices: the compact width is a disk
// detail, never a second in-memory IR.
//
// Parsing is hostile-input safe (third-party circuit files are a goal): fixed
// little-endian field reads (never a struct overlay / reinterpret_cast, so the
// reader is endian- and layout-independent), overflow-checked size math, an
// exact total-size check, and validate_program() after decode. Trusted
// self-produced assets (the shipped float circuits) take the same path; the
// cost is negligible and it keeps one loader.
//
// Layout (all integers little-endian):
//   magic   : 4 bytes = 'E','M','P','B'
//   version : u16
//   index_width : u8  (2 or 4)
//   flags   : u8  (bits 0-1 = WireReuse {None=0,Linear=1,Full=2}; value 3 and
//                  bits 2-7 reserved, must be 0)
//   num_wires   : u32
//   num_inputs  : u32
//   num_outputs : u32
//   num_gates   : u32
//   gates   : num_gates × { in0, in1, out (each index_width bytes), op u8,
//                           zero padding to the next index slot }
//             record size = 4*index_width (u16 form: 8 bytes; u32 form: 16)
//   outputs : num_outputs × index_width bytes

#include "emp-tool/ir/program.h"
#include "emp-tool/ir/validate.h"
#include "emp-tool/runtime/core/utils.h"   // error()
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace emp {
namespace circuit {

namespace empbc_detail {

constexpr uint8_t MAGIC[4]   = {'E', 'M', 'P', 'B'};
constexpr uint16_t VERSION   = 1;
constexpr size_t  HEADER_LEN = 4 + 2 + 1 + 1 + 4 + 4 + 4 + 4;   // 24

// --- little-endian writers ---
inline void put_u8 (std::vector<uint8_t>& b, uint8_t v)  { b.push_back(v); }
inline void put_u16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); }
inline void put_u32(std::vector<uint8_t>& b, uint32_t v) {
	for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xFF);
}
inline void put_idx(std::vector<uint8_t>& b, uint32_t v, int iw) {
	if (iw == 2) put_u16(b, (uint16_t)v); else put_u32(b, v);
}

// --- bounds-checked little-endian reader over a byte span ---
struct Reader {
	const uint8_t* p;
	size_t n, pos = 0;
	Reader(const uint8_t* p_, size_t n_) : p(p_), n(n_) {}
	void need(size_t k) const {
		expecting(pos <= n && k <= n - pos, "empbc: truncated file");
	}
	uint8_t  u8()  { need(1); return p[pos++]; }
	uint16_t u16() { need(2); uint16_t v = (uint16_t)p[pos] | ((uint16_t)p[pos + 1] << 8); pos += 2; return v; }
	uint32_t u32() {
		need(4);
		uint32_t v = (uint32_t)p[pos] | ((uint32_t)p[pos + 1] << 8) |
		             ((uint32_t)p[pos + 2] << 16) | ((uint32_t)p[pos + 3] << 24);
		pos += 4; return v;
	}
	uint32_t idx(int iw) { return iw == 2 ? (uint32_t)u16() : u32(); }
};

}  // namespace empbc_detail

// Serialize a program to .empbc bytes. Auto-selects the compact 16-bit index
// form when every wire id fits. Precondition: `p` is a valid program (caller
// validates; we don't silently emit a malformed file).
inline std::vector<uint8_t> save_empbc(const BooleanProgram& p) {
	using namespace empbc_detail;
	// Validate first: a malformed program (e.g. a wire id >= num_wires while
	// num_wires <= 0xFFFF) would otherwise be silently truncated by the 16-bit
	// writer. validate_program guarantees every id < num_wires, so the chosen
	// index width always fits.
	validate_program(p);
	const int iw = (p.num_wires <= 0xFFFFu) ? 2 : 4;

	std::vector<uint8_t> b;
	b.reserve(HEADER_LEN + (size_t)p.gates.size() * (4 * iw) +
	          (size_t)p.outputs.size() * iw);
	for (uint8_t m : MAGIC) put_u8(b, m);
	put_u16(b, VERSION);
	put_u8(b, (uint8_t)iw);
	put_u8(b, (uint8_t)p.wire_reuse);   // flags: bits 0-1 = wire_reuse
	put_u32(b, p.num_wires);
	put_u32(b, p.num_inputs);
	put_u32(b, (uint32_t)p.outputs.size());
	put_u32(b, (uint32_t)p.gates.size());
	for (const Gate& g : p.gates) {
		put_idx(b, g.in0, iw);
		put_idx(b, g.in1, iw);
		put_idx(b, g.out, iw);
		put_u8(b, (uint8_t)g.op);
		put_u8(b, 0);                   // reserved (u16 form: 1 byte; u32 form: 3)
		if (iw == 4) { put_u8(b, 0); put_u8(b, 0); }
	}
	for (uint32_t o : p.outputs) put_idx(b, o, iw);
	return b;
}

// Decode .empbc bytes into a runtime BooleanProgram (uint32_t indices) and
// validate it. error()s (fatal) on any structural problem.
inline BooleanProgram load_empbc(const uint8_t* bytes, size_t len) {
	using namespace empbc_detail;
	Reader r(bytes, len);

	for (uint8_t m : MAGIC)
		expecting(r.u8() == m, "empbc: bad magic");
	uint16_t version = r.u16();
	expecting(version == VERSION, "empbc: unsupported version");
	int iw = r.u8();
	expecting(iw == 2 || iw == 4, "empbc: bad index_width");
	uint8_t flags = r.u8();
	expecting((flags & ~0x3u) == 0, "empbc: unknown flags bits set");
	expecting((flags & 0x3u) != 3, "empbc: reserved wire_reuse value");

	BooleanProgram p;
	p.wire_reuse           = (WireReuse)(flags & 0x3u);
	p.num_wires            = r.u32();
	p.num_inputs           = r.u32();
	uint32_t num_outputs   = r.u32();
	uint32_t num_gates     = r.u32();

	// Overflow-checked exact size: header + gates + outputs must equal len.
	// Gate record = 3 index slots + op + reserved, where op+reserved is padded
	// to one more index slot (u16: 1+1; u32: 1+3) — so 4*iw bytes total.
	const uint64_t grec = (uint64_t)4 * iw;
	const uint64_t expect =
	    (uint64_t)HEADER_LEN + (uint64_t)num_gates * grec + (uint64_t)num_outputs * (uint64_t)iw;
	expecting(expect == (uint64_t)len,
	          "empbc: declared sizes do not match file length");

	p.gates.reserve(num_gates);
	for (uint32_t i = 0; i < num_gates; ++i) {
		Gate g;
		g.in0 = r.idx(iw);
		g.in1 = r.idx(iw);
		g.out = r.idx(iw);
		uint8_t op = r.u8();
		expecting(op <= (uint8_t)Op::Const1, "empbc: unknown op code");
		g.op = (Op)op;
		r.u8();                          // reserved
		if (iw == 4) { r.u8(); r.u8(); }
		p.gates.push_back(g);
	}
	p.outputs.reserve(num_outputs);
	for (uint32_t i = 0; i < num_outputs; ++i) p.outputs.push_back(r.idx(iw));

	validate_program(p);
	return p;
}

inline BooleanProgram load_empbc(const std::vector<uint8_t>& bytes) {
	return load_empbc(bytes.data(), bytes.size());
}

// No cleanup on the failure paths: expecting() ends the process, so an
// open FILE* cannot leak into continued execution.
inline BooleanProgram load_empbc_file(const char* path) {
	FILE* f = std::fopen(path, "rb");
	expecting(f != nullptr,
	          [&] { return std::string("empbc: cannot open ") + path; });
	expecting(std::fseek(f, 0, SEEK_END) == 0, "empbc: seek failed");
	long sz = std::ftell(f);
	expecting(sz >= 0, "empbc: tell failed");
	std::rewind(f);
	std::vector<uint8_t> buf((size_t)sz);
	expecting(sz == 0 || std::fread(buf.data(), 1, (size_t)sz, f) == (size_t)sz,
	          "empbc: short read");
	std::fclose(f);
	return load_empbc(buf);
}

inline void save_empbc_file(const char* path, const BooleanProgram& p) {
	std::vector<uint8_t> b = save_empbc(p);
	FILE* f = std::fopen(path, "wb");
	expecting(f != nullptr, [&] {
		return std::string("empbc: cannot open for write ") + path;
	});
	bool ok = b.empty() || std::fwrite(b.data(), 1, b.size(), f) == b.size();
	std::fclose(f);
	expecting(ok, "empbc: short write");
}

}  // namespace circuit
}  // namespace emp
#endif  // EMP_IR_EMPBC_H__
