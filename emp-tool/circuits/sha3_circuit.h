#ifndef EMP_SHA3_CIRCUIT_H_
#define EMP_SHA3_CIRCUIT_H_

#include "emp-tool/circuits/bit.h"
#include <cstddef>
#include <cstdint>

namespace emp {

// Keccak-f[1600] permutation as hardcoded gates. State is 5x5 lanes of 64
// bits = 1600 bits, laid out per FIPS-202: bit z of lane (x, y) lives at
// state[64*(5*y + x) + z], with z=0 = LSB of the lane. Bit/byte ordering for
// the sponge wrapper (sha3_256.h) matches the rest of emp-tool: LSB-first
// within each byte, byte-sequential. The byte-i, bit-k mapping into the
// state is then  state_index = 64*(byte_i/8) + 8*(byte_i%8) + k  (FIPS-202
// §B.1; lane_idx = byte_i/8 unpacks to (x, y) = (lane_idx%5, lane_idx/5)).
//
// chi is the only nonlinear step: 1 AND/bit * 1600 bits * 24 rounds = 38400
// ANDs per permute. theta/rho/pi/iota are pure XOR / index relabel /
// public-constant XOR; zero ANDs.

namespace keccak_detail {

// Rho rotation offsets, indexed RHO[y][x] (rows are y = 0..4).
inline constexpr int RHO[5][5] = {
	{  0,  1, 62, 28, 27 },
	{ 36, 44,  6, 55, 20 },
	{  3, 10, 43, 25, 39 },
	{ 41, 45, 15, 21,  8 },
	{ 18,  2, 61, 56, 14 },
};

// Iota round constants, FIPS-202 §3.2.5.
inline constexpr uint64_t RC[24] = {
	0x0000000000000001ULL, 0x0000000000008082ULL,
	0x800000000000808aULL, 0x8000000080008000ULL,
	0x000000000000808bULL, 0x0000000080000001ULL,
	0x8000000080008081ULL, 0x8000000000008009ULL,
	0x000000000000008aULL, 0x0000000000000088ULL,
	0x0000000080008009ULL, 0x000000008000000aULL,
	0x000000008000808bULL, 0x800000000000008bULL,
	0x8000000000008089ULL, 0x8000000000008003ULL,
	0x8000000000008002ULL, 0x8000000000000080ULL,
	0x000000000000800aULL, 0x800000008000000aULL,
	0x8000000080008081ULL, 0x8000000000008080ULL,
	0x0000000080000001ULL, 0x8000000080008008ULL,
};

constexpr inline std::size_t IDX(int x, int y, int z) {
	return 64 * (5 * y + x) + z;
}

}  // namespace keccak_detail

// theta: column parity + diffusion. Pure XOR.
//   C[x][z] = A[x,0,z] ^ A[x,1,z] ^ A[x,2,z] ^ A[x,3,z] ^ A[x,4,z]
//   D[x][z] = C[x-1][z] ^ rotL(C[x+1], 1)[z] = C[x-1][z] ^ C[x+1][(z-1)%64]
//   A'[x,y,z] = A[x,y,z] ^ D[x][z]
template <typename Wire>
inline void keccak_theta(Bit_T<Wire> A[1600]) {
	using B = Bit_T<Wire>;
	using namespace keccak_detail;

	B C[5][64];
	for (int x = 0; x < 5; ++x)
		for (int z = 0; z < 64; ++z)
			C[x][z] = A[IDX(x, 0, z)] ^ A[IDX(x, 1, z)] ^ A[IDX(x, 2, z)]
			        ^ A[IDX(x, 3, z)] ^ A[IDX(x, 4, z)];

	B D[5][64];
	for (int x = 0; x < 5; ++x)
		for (int z = 0; z < 64; ++z)
			D[x][z] = C[(x + 4) % 5][z] ^ C[(x + 1) % 5][(z + 63) % 64];

	for (int x = 0; x < 5; ++x)
		for (int y = 0; y < 5; ++y)
			for (int z = 0; z < 64; ++z)
				A[IDX(x, y, z)] = A[IDX(x, y, z)] ^ D[x][z];
}

// Fused rho + pi: B[y, 2x+3y, z'] = A[x, y, z]  where z' = (z + RHO[y][x]) %
// 64. Pure index relabel, zero gates. Uses a 1600-element scratch buffer
// then copies back to A in natural state order.
template <typename Wire>
inline void keccak_rho_pi(Bit_T<Wire> A[1600]) {
	using B = Bit_T<Wire>;
	using namespace keccak_detail;

	B Bt[1600];
	for (int x = 0; x < 5; ++x)
		for (int y = 0; y < 5; ++y) {
			const int new_x = y;
			const int new_y = (2 * x + 3 * y) % 5;
			const int r = RHO[y][x];
			for (int z = 0; z < 64; ++z)
				Bt[IDX(new_x, new_y, (z + r) % 64)] = A[IDX(x, y, z)];
		}
	for (int i = 0; i < 1600; ++i) A[i] = Bt[i];
}

// chi: A'[x,y,z] = A[x,y,z] ^ ((NOT A[(x+1)%5,y,z]) AND A[(x+2)%5,y,z]).
// Snapshot each y-row first so the in-place writes don't clobber sources.
// 1 AND per bit; 1600 ANDs total per call.
template <typename Wire>
inline void keccak_chi(Bit_T<Wire> A[1600]) {
	using B = Bit_T<Wire>;
	using namespace keccak_detail;

	for (int y = 0; y < 5; ++y) {
		B row[5][64];
		for (int x = 0; x < 5; ++x)
			for (int z = 0; z < 64; ++z)
				row[x][z] = A[IDX(x, y, z)];
		for (int x = 0; x < 5; ++x)
			for (int z = 0; z < 64; ++z)
				A[IDX(x, y, z)] =
					row[x][z] ^ ((!row[(x + 1) % 5][z]) & row[(x + 2) % 5][z]);
	}
}

// iota: XOR public round constant RC[round] into lane (0,0). Public
// constant ⇒ implement as `state[k] = !state[k]` for each set bit (zero
// ANDs; matches Rcon handling at aes_circuit.h:196).
template <typename Wire>
inline void keccak_iota(Bit_T<Wire> A[1600], int round) {
	using namespace keccak_detail;
	const uint64_t rc = RC[round];
	for (int z = 0; z < 64; ++z)
		if ((rc >> z) & 1) A[IDX(0, 0, z)] = !A[IDX(0, 0, z)];
}

// Keccak-f[1600] permutation: 24 rounds of theta -> rho/pi -> chi -> iota,
// in-place on the 1600-bit state.
template <typename Wire>
class Keccak_F_Calculator_T {
public:
	using B = Bit_T<Wire>;

	void permute(B state[1600]) {
		for (int r = 0; r < 24; ++r) {
			keccak_theta<Wire>(state);
			keccak_rho_pi<Wire>(state);
			keccak_chi<Wire>(state);
			keccak_iota<Wire>(state, r);
		}
	}
};

}  // namespace emp

#endif  // EMP_SHA3_CIRCUIT_H_
