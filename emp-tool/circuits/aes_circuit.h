#ifndef EMP_AES_CIRCUIT_H_
#define EMP_AES_CIRCUIT_H_

#include "emp-tool/circuits/bit.h"

namespace emp {

// AES SBox (Boyar-Peralta 2010): 32 ANDs, 81 XORs, 4 NOTs.
// Public bit convention: U[0]=LSB, U[7]=MSB; S[0]=LSB, S[7]=MSB.
// (BP paper uses MSB-first; we flip indices once at entry/exit so the
// published formulas stay verbatim. Pure renaming — no gates added.)
template <typename Wire>
inline void aes_sbox(const Bit_T<Wire> U_lsb[8], Bit_T<Wire> S_lsb[8]) {
	using B = Bit_T<Wire>;

	B U[8];
	for (int i = 0; i < 8; ++i) U[i] = U_lsb[7 - i];

	B t1  = U[3] ^ U[5];
	B t2  = U[0] ^ U[6];
	B t3  = U[0] ^ U[3];
	B t4  = U[0] ^ U[5];
	B t5  = U[1] ^ U[2];
	B t6  = t5  ^ U[7];
	B t7  = t6  ^ U[3];
	B t8  = t2  ^ t1;
	B t9  = t6  ^ U[0];
	B t10 = t6  ^ U[6];
	B t11 = t10 ^ t4;
	B t12 = U[4] ^ t8;
	B t13 = t12 ^ U[5];
	B t14 = t12 ^ U[1];
	B t15 = t13 ^ U[7];
	B t16 = t13 ^ t5;
	B t17 = t14 ^ t3;
	B t18 = U[7] ^ t17;
	B t19 = t16 ^ t17;
	B t20 = t16 ^ t4;
	B t21 = t5  ^ t17;
	B t22 = t2  ^ t21;
	B t23 = U[0] ^ t21;

	B t24 = t8  & t13;
	B t25 = t11 & t15;
	B t27 = t7  & U[7];
	B t29 = t2  & t21;
	B t30 = t10 & t6;
	B t32 = t9  & t18;
	B t34 = t3  & t17;
	B t35 = t1  & t19;
	B t37 = t4  & t16;
	B t26 = t25 ^ t24;
	B t28 = t27 ^ t24;
	B t31 = t30 ^ t29;
	B t33 = t32 ^ t29;
	B t36 = t35 ^ t34;
	B t38 = t37 ^ t34;
	B t39 = t26 ^ t14;
	B t40 = t28 ^ t38;
	B t41 = t31 ^ t36;
	B t42 = t33 ^ t38;
	B t43 = t39 ^ t36;
	B t44 = t40 ^ t20;
	B t45 = t41 ^ t22;
	B t46 = t42 ^ t23;

	B t47 = t43 ^ t44;
	B t48 = t43 & t45;
	B t49 = t46 ^ t48;
	B t50 = t47 & t49;
	B t51 = t50 ^ t44;
	B t52 = t45 ^ t46;
	B t53 = t44 ^ t48;
	B t54 = t53 & t52;
	B t55 = t54 ^ t46;
	B t56 = t45 ^ t55;
	B t57 = t49 ^ t55;
	B t58 = t46 & t57;
	B t59 = t58 ^ t56;
	B t60 = t49 ^ t58;
	B t61 = t51 & t60;
	B t62 = t47 ^ t61;
	B t63 = t62 ^ t59;
	B t64 = t51 ^ t55;
	B t65 = t51 ^ t62;
	B t66 = t55 ^ t59;
	B t67 = t64 ^ t63;

	B t68 = t66 & t13;
	B t69 = t59 & t15;
	B t70 = t55 & U[7];
	B t71 = t65 & t21;
	B t72 = t62 & t6;
	B t73 = t51 & t18;
	B t74 = t64 & t17;
	B t75 = t67 & t19;
	B t76 = t63 & t16;
	B t77 = t66 & t8;
	B t78 = t59 & t11;
	B t79 = t55 & t7;
	B t80 = t65 & t2;
	B t81 = t62 & t10;
	B t82 = t51 & t9;
	B t83 = t64 & t3;
	B t84 = t67 & t1;
	B t85 = t63 & t4;

	B t86 = t83 ^ t84;
	B t87 = t78 ^ t86;
	B t88 = t77 ^ t87;
	B t89 = t68 ^ t70;
	B t90 = t69 ^ t68;
	B t91 = t71 ^ t72;
	B t92 = t80 ^ t89;
	B t93 = t75 ^ t91;
	B t94 = t76 ^ t92;
	B t95 = t93 ^ t94;
	B t96 = t91 ^ t90;
	B t97 = t71 ^ t73;
	B t98 = t81 ^ t86;
	B t99 = t89 ^ t97;
	B t100 = t74 ^ t93;
	B t101 = t82 ^ t95;
	B t102 = t98 ^ t99;
	B t103 = t83 ^ t100;
	B t104 = t87 ^ t79;
	B t105 = t101 ^ t103;

	// S3 must be computed before S1 / S4 (they reference it).
	B S[8];
	S[0] = t88 ^ t100;
	S[3] = t88 ^ t96;
	S[1] = !(S[3] ^ t100);
	S[2] = !(t105 ^ t85);
	S[4] = t99 ^ S[3];
	S[5] = t104 ^ t101;
	S[6] = !(t95 ^ t102);
	S[7] = !(t80 ^ t102);

	for (int i = 0; i < 8; ++i) S_lsb[i] = S[7 - i];
}

// xtime: multiply by x in GF(2^8) with poly x^8+x^4+x^3+x+1.
// LSB-first: in[0]=bit 0, in[7]=bit 7. Pure XOR, no ANDs.
template <typename Wire>
inline void aes_xtime(const Bit_T<Wire> in[8], Bit_T<Wire> out[8]) {
	const Bit_T<Wire> &m = in[7];
	out[0] = m;
	out[1] = in[0] ^ m;
	out[2] = in[1];
	out[3] = in[2] ^ m;
	out[4] = in[3] ^ m;
	out[5] = in[4];
	out[6] = in[5];
	out[7] = in[6];
}

// AES-128 in-circuit calculator. Bit convention: LSB at index 0 within
// each byte; bytes packed in natural order (byte i at offsets 8i..8i+7).
// State is column-major 4×4: byte (col, row) at offset 8*(4*col + row).
template <typename Wire>
class AES_Calculator_T {
public:
	using B = Bit_T<Wire>;

	// AES-128 key expansion: 128-bit key -> 11 round keys (1408 bits total).
	// expanded[r*128 .. (r+1)*128) holds round key r. 40 SBox calls.
	void key_schedule(const B key[128], B expanded[1408]) {
		for (int i = 0; i < 128; ++i) expanded[i] = key[i];

		static const unsigned char Rcon[11] = {
			0x00, 0x01, 0x02, 0x04, 0x08, 0x10,
			0x20, 0x40, 0x80, 0x1b, 0x36
		};

		for (int r = 1; r <= 10; ++r) {
			B *prev = expanded + (r - 1) * 128;
			B *cur  = expanded + r * 128;

			B tmp[32];
			// RotWord on bytes 12..15 of prev: [b12,b13,b14,b15] -> [b13,b14,b15,b12]
			for (int b = 0; b < 4; ++b) {
				int src_byte = 12 + ((b + 1) & 3);
				for (int k = 0; k < 8; ++k)
					tmp[b * 8 + k] = prev[src_byte * 8 + k];
			}
			// SubWord: SBox each of the 4 bytes.
			for (int b = 0; b < 4; ++b) {
				B in[8], out[8];
				for (int k = 0; k < 8; ++k) in[k] = tmp[b * 8 + k];
				aes_sbox<Wire>(in, out);
				for (int k = 0; k < 8; ++k) tmp[b * 8 + k] = out[k];
			}
			// XOR Rcon[r] into byte 0 of tmp (LSB-first bit indexing).
			for (int k = 0; k < 8; ++k) {
				if ((Rcon[r] >> k) & 1) tmp[k] = !tmp[k];
			}

			// Word 0 of cur = word 0 of prev XOR tmp
			for (int k = 0; k < 32; ++k) cur[k] = prev[k] ^ tmp[k];
			// Words 1..3 of cur = word w-1 of cur XOR word w of prev
			for (int w = 1; w < 4; ++w) {
				for (int k = 0; k < 32; ++k)
					cur[w * 32 + k] = cur[(w - 1) * 32 + k] ^ prev[w * 32 + k];
			}
		}
	}

	// AES-128 encrypt with already-expanded key. 160 SBox calls.
	void encrypt(const B plaintext[128], const B expanded_key[1408],
	             B ciphertext[128]) {
		B state[128];
		for (int i = 0; i < 128; ++i)
			state[i] = plaintext[i] ^ expanded_key[i];

		for (int r = 1; r <= 10; ++r) {
			// SubBytes: 16 SBoxes
			for (int b = 0; b < 16; ++b) {
				B in[8], out[8];
				for (int k = 0; k < 8; ++k) in[k] = state[b * 8 + k];
				aes_sbox<Wire>(in, out);
				for (int k = 0; k < 8; ++k) state[b * 8 + k] = out[k];
			}

			// ShiftRows: new state[c, row] = old state[(c+row) mod 4, row]
			B shifted[128];
			for (int c = 0; c < 4; ++c)
				for (int row = 0; row < 4; ++row) {
					int src_byte = 4 * ((c + row) & 3) + row;
					int dst_byte = 4 * c + row;
					for (int k = 0; k < 8; ++k)
						shifted[dst_byte * 8 + k] = state[src_byte * 8 + k];
				}
			for (int i = 0; i < 128; ++i) state[i] = shifted[i];

			// MixColumns (skip on final round)
			if (r != 10) {
				B mixed[128];
				for (int c = 0; c < 4; ++c) {
					const B *col = state + c * 32;
					const B *a = col + 0;
					const B *b = col + 8;
					const B *cc = col + 16;
					const B *d = col + 24;

					B t[8], ab[8], bc[8], cd[8], da[8];
					B xab[8], xbc[8], xcd[8], xda[8];
					for (int k = 0; k < 8; ++k) {
						t[k]  = a[k] ^ b[k] ^ cc[k] ^ d[k];
						ab[k] = a[k] ^ b[k];
						bc[k] = b[k] ^ cc[k];
						cd[k] = cc[k] ^ d[k];
						da[k] = d[k] ^ a[k];
					}
					aes_xtime<Wire>(ab, xab);
					aes_xtime<Wire>(bc, xbc);
					aes_xtime<Wire>(cd, xcd);
					aes_xtime<Wire>(da, xda);

					B *na = mixed + c * 32 + 0;
					B *nb = mixed + c * 32 + 8;
					B *nc = mixed + c * 32 + 16;
					B *nd = mixed + c * 32 + 24;
					for (int k = 0; k < 8; ++k) {
						na[k] = a[k]  ^ t[k] ^ xab[k];
						nb[k] = b[k]  ^ t[k] ^ xbc[k];
						nc[k] = cc[k] ^ t[k] ^ xcd[k];
						nd[k] = d[k]  ^ t[k] ^ xda[k];
					}
				}
				for (int i = 0; i < 128; ++i) state[i] = mixed[i];
			}

			// AddRoundKey r
			const B *rk = expanded_key + r * 128;
			for (int i = 0; i < 128; ++i) state[i] = state[i] ^ rk[i];
		}

		for (int i = 0; i < 128; ++i) ciphertext[i] = state[i];
	}

	// Convenience: key_schedule + encrypt in one call.
	void encrypt_with_key(const B plaintext[128], const B key[128],
	                      B ciphertext[128]) {
		B expanded[1408];
		key_schedule(key, expanded);
		encrypt(plaintext, expanded, ciphertext);
	}
};

}  // namespace emp

#endif  // EMP_AES_CIRCUIT_H_
