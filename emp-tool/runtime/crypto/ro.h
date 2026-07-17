#ifndef EMP_RO_H__
#define EMP_RO_H__

#include "emp-tool/runtime/core/block.h"
#include "emp-tool/runtime/core/utils.h"
#include "emp-tool/runtime/crypto/ec.h"
#include "emp-tool/runtime/crypto/hash.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace emp {

// Random-oracle helper. Absorb heterogeneous fields, then squeeze a 16-byte
// block, a 32-byte digest, or a curve Point. Domain separation is the
// caller's responsibility: construct with a domain string and a session id
// (mandatory), then absorb the per-call inputs.
//
// Encoding. The absorbed message M is a sequence of typed, length-framed
// fields. Each absorb overload has a fixed 1-based type id; a field of `len`
// bytes is framed as:
//
//     u32_LE(type) || u32_LE(len) || bytes        (len must fit in 32 bits)
//
// The type tag makes M injective in the *call sequence*, not merely the
// bytes: absorb(block) and absorb(ptr, 16) of the same 16 bytes produce
// different frames, so a sequence of absorb calls maps 1-1 to M and no field
// content can forge a frame boundary. (u32 type + u32 len occupy the 8 bytes
// a u64 length alone would.)
class RO {
public:
	// sid is mandatory — every RO is session-bound. Pass zero_block
	// explicitly for the (rare) genuinely session-independent oracle.
	RO(std::string_view domain, block sid) : domain_(domain) {
		frame(kStr, domain.data(), domain.size());
		frame(kBlock, &sid, sizeof(block));
	}

	// One overload per field type; the type id is baked into the frame.
	RO& absorb(std::string_view s)        { return frame(kStr,   s.data(), s.size()); }
	RO& absorb(const void* p, size_t n)   { return frame(kBytes, p, n); }
	RO& absorb(block b)                   { return frame(kBlock, &b, sizeof(block)); }
	RO& absorb(uint64_t v)                { return frame(kU64,   &v, sizeof(v)); }
	RO& absorb(const Point& p) {
		size_t n = p.size();
		std::vector<unsigned char> tmp(n);
		p.to_bin(tmp.data(), n);
		return frame(kPoint, tmp.data(), n);
	}

	// Terminals over the absorbed message M.
	block squeeze_block() const {
		return Hash::hash_for_block(buf_.data(), (int64_t)buf_.size());
	}
	void squeeze_digest(void* out32) const {
		Hash::hash_once(out32, buf_.data(), (int64_t)buf_.size());
	}
	// hash-to-curve over M, with the domain string as the RFC 9380 DST.
	Point squeeze_point(ECGroup& g) const {
		return g.hash_to_point((const char*)buf_.data(), buf_.size(),
		                       domain_.data(), domain_.size());
	}

private:
	// absorb type ids (1-based); each overload owns one.
	static constexpr uint32_t kStr   = 1;
	static constexpr uint32_t kBytes = 2;
	static constexpr uint32_t kBlock = 3;
	static constexpr uint32_t kU64   = 4;
	static constexpr uint32_t kPoint = 5;

	RO& frame(uint32_t type, const void* p, size_t n) {
		expecting(n <= 0xFFFFFFFFull,
		          "RO::absorb: field length exceeds 2^32-1");
		uint32_t len = (uint32_t)n;
		size_t off = buf_.size();
		buf_.resize(off + 2 * sizeof(uint32_t) + n);
		std::memcpy(buf_.data() + off, &type, sizeof(uint32_t));
		std::memcpy(buf_.data() + off + sizeof(uint32_t), &len, sizeof(uint32_t));
		if (n) std::memcpy(buf_.data() + off + 2 * sizeof(uint32_t), p, n);
		return *this;
	}

	std::vector<unsigned char> buf_;   // accumulated M, every field typed + length-framed
	std::string domain_;               // retained for squeeze_point DST
};

}  // namespace emp
#endif  // EMP_RO_H__
