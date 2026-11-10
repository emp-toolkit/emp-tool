#ifndef EMP_CIRCUIT_CRYPTO_AES128_H__
#define EMP_CIRCUIT_CRYPTO_AES128_H__

#include "emp-tool/ir/context/concept.h"
#include "emp-tool/circuits/bitvec.h"
#include "emp-tool/circuits/unsigned_int.h"   // for the CTR counter increment
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace emp {
namespace circuit {
namespace crypto {

// AES-128 over the BooleanContext value layer. The public API is BitVec_T-first:
// blocks, keys, IVs, and byte values are typed bit-vectors. Implementation helpers
// operate on bare Ctx::Wire arrays so the bulk circuit state does not become
// Bit_T-per-bit storage. Round constants fold in with NOT gates, so no public
// constants are needed. `aes128_encrypt` is the block primitive; `aes128_ctr`
// provides CTR mode (NIST SP 800-38A) on top of it. Other modes (CBC, ...) are
// caller-composed from the block primitive. Bit convention: LSB at index 0 within
// each byte; bytes natural order; state column-major 4x4.

namespace detail {

// AES S-box: 28 ANDs at AND-depth 4, 103 XORs, 27 NOTs. Straight-line program
// aes-sbox-a28-ad4-g131-gd32-xx103-47 of the NIST Circuit Complexity repository
// (usnistgov/Circuits; contributed by Milad Nasr, 2026): Boyar-Peralta tower
// field with 9 level-one products, a 6-AND GF(2^4) inversion at AND-depth 2 and
// 13 output products. Its XNORs are XORs with the complement carried along and
// a NOT only where a complemented wire meets an AND or an output.
// Public bit convention: U[0]=LSB, U[7]=MSB; S[0]=LSB, S[7]=MSB. The program
// names bits MSB-first, so indices flip once at entry/exit (no gates).
template <BooleanContext Ctx>
inline void aes_sbox(Ctx& ctx, const typename Ctx::Wire U_lsb[8], typename Ctx::Wire S_lsb[8]) {
	using W = typename Ctx::Wire;
	auto X  = [&](W a, W b) { return ctx.xor_gate(a, b); };
	auto An = [&](W a, W b) { return ctx.and_gate(a, b); };
	auto Nt = [&](W a)      { return ctx.not_gate(a); };
	W U[8];
	for (int i = 0; i < 8; ++i) U[i] = U_lsb[7 - i];
	const W w_t1 = X(U[2], U[7]);
	const W w_t2 = X(U[0], U[3]);
	const W w_t3 = X(U[0], U[6]);
	const W w_t4 = X(U[3], U[5]);
	const W w_t5 = X(U[0], U[5]);
	const W w_t6 = X(U[1], w_t1);
	const W w_t7 = X(U[4], w_t1);
	const W w_t8 = X(w_t4, w_t3);
	const W w_t9 = X(U[6], w_t6);
	const W w_t10 = X(U[0], w_t6);
	const W w_a1 = An(w_t6, w_t3);
	const W w_t11 = X(w_t7, w_t9);
	const W w_t12 = X(w_t2, w_t11);
	const W w_t13 = X(w_t8, w_t11);
	const W w_t14 = X(U[5], w_t11);
	const W w_t15 = X(U[2], w_t12);
	const W w_t16 = X(U[7], w_t14);
	const W w_t17 = X(U[1], w_t14);
	const W n_t1 = Nt(w_t1);
	const W n_t13 = Nt(w_t13);
	const W w_a2 = An(n_t1, n_t13);
	const W w_a3 = An(U[4], w_t12);
	const W n_t14 = Nt(w_t14);
	const W n_t2 = Nt(w_t2);
	const W w_a4 = An(n_t14, n_t2);
	const W w_t18 = X(w_a1, w_a2);
	const W w_t19 = X(w_t14, w_t15);
	const W w_t20 = X(w_a2, w_a4);
	const W w_t21 = X(U[2], w_t17);
	const W w_a5 = An(w_t16, w_t9);
	const W n_t5 = Nt(w_t5);
	const W n_t15 = Nt(w_t15);
	const W w_a6 = An(n_t5, n_t15);
	const W n_t17 = Nt(w_t17);
	const W n_t7 = Nt(w_t7);
	const W w_a7 = An(n_t17, n_t7);
	const W w_t22 = X(w_a3, w_a5);
	const W w_t23 = X(w_t20, w_t17);
	const W w_t24 = X(w_a1, w_a5);
	const W w_t25 = X(w_a7, w_t13);
	const W w_t26 = X(w_t6, w_t19);
	const W n_t4 = Nt(w_t4);
	const W n_t19 = Nt(w_t19);
	const W w_a8 = An(n_t4, n_t19);
	const W w_a9 = An(w_t10, w_t21);
	const W w_t27 = X(w_t18, w_t22);
	const W w_t28 = X(w_a7, w_a8);
	const W w_t29 = X(w_a9, w_t24);
	const W w_t30 = X(w_a6, w_t28);
	const W w_t31 = X(w_t28, w_t23);
	const W w_t32 = X(w_t29, w_t25);
	const W w_t33 = X(w_a3, w_t30);
	const W w_t34 = X(w_t22, w_t32);
	const W w_t35 = X(w_t18, w_t32);
	const W w_t36 = X(w_a8, w_t32);
	const W w_t37 = X(w_t33, w_t31);
	const W w_a10 = An(w_t31, w_t35);
	const W w_t38 = X(w_a10, w_t34);
	const W n_t34 = Nt(w_t34);
	const W w_a11 = An(w_t37, n_t34);
	const W w_t39 = X(w_a11, w_t38);
	const W w_t40 = X(w_a11, w_t35);
	const W w_a12 = An(w_t38, w_t33);
	const W w_t41 = X(w_a12, w_t39);
	const W n_t27 = Nt(w_t27);
	const W n_t39 = Nt(w_t39);
	const W w_a13 = An(n_t27, n_t39);
	const W w_a14 = An(n_t34, n_t39);
	const W n_t40 = Nt(w_t40);
	const W w_a15 = An(n_t40, w_t33);
	const W w_t42 = X(w_a13, w_t35);
	const W w_t43 = X(w_a14, w_t1);
	const W w_t44 = X(w_a11, w_a15);
	const W w_t45 = X(w_t17, w_t41);
	const W w_t46 = X(w_t15, w_t42);
	const W w_t47 = X(w_t43, w_t12);
	const W w_t48 = X(U[3], w_t44);
	const W w_t49 = X(U[6], w_t45);
	const W w_t50 = X(w_t1, w_t44);
	const W w_t51 = X(w_t44, w_t13);
	const W w_t52 = X(U[4], w_t42);
	const W w_t53 = X(w_t48, w_t49);
	const W w_t54 = X(w_t26, w_t46);
	const W w_t55 = X(w_t5, w_t50);
	const W n_t45 = Nt(w_t45);
	const W w_a16 = An(n_t45, w_t51);
	const W n_t43 = Nt(w_t43);
	const W n_t52 = Nt(w_t52);
	const W w_a17 = An(n_t43, n_t52);
	const W w_t56 = X(w_t26, w_t53);
	const W w_t57 = X(w_t50, w_t54);
	const W w_t58 = X(w_a12, w_a16);
	const W n_t47 = Nt(w_t47);
	const W n_t54 = Nt(w_t54);
	const W w_a18 = An(n_t47, n_t54);
	const W w_a19 = An(w_t55, w_t53);
	const W w_t59 = X(w_t2, w_t56);
	const W w_t60 = X(w_t8, w_t57);
	const W w_t61 = X(w_a18, w_t37);
	const W w_a20 = An(w_t56, w_t48);
	const W w_t62 = X(w_t47, w_t59);
	const W w_t63 = X(w_t46, w_t60);
	const W w_t64 = X(U[1], w_t59);
	const W w_t65 = X(U[7], w_t60);
	const W w_t66 = X(w_a14, w_t61);
	const W w_t67 = X(w_t5, w_t62);
	const W w_t68 = X(w_t36, w_t66);
	const W w_a21 = An(w_t63, w_t64);
	const W w_t69 = X(w_t46, w_t67);
	const W w_t70 = X(w_t14, w_t67);
	const W w_t71 = X(w_a20, w_t68);
	const W w_t72 = X(U[3], w_t69);
	const W w_t73 = X(w_t49, w_t69);
	const W w_t74 = X(w_t50, w_t70);
	const W w_t75 = X(U[7], w_t69);
	const W w_a22 = An(w_t57, w_t69);
	const W w_t76 = X(w_t6, w_t72);
	const W w_t77 = X(w_t2, w_t73);
	const W w_t78 = X(w_t15, w_t72);
	const W w_t79 = X(w_a18, w_a22);
	const W w_a23 = An(w_t62, w_t73);
	const W n_t74 = Nt(w_t74);
	const W w_a24 = An(n_t74, w_t75);
	const W w_t80 = X(w_t19, w_t77);
	const W w_t81 = X(w_a23, w_t71);
	const W w_a25 = An(w_t76, w_t60);
	const W w_a26 = An(w_t67, w_t77);
	const W w_a27 = An(w_t65, w_t78);
	const W w_t82 = X(w_a18, w_a26);
	const W w_t83 = X(w_a25, w_t58);
	const W w_t84 = X(w_a27, w_t79);
	const W w_S2 = X(w_t58, w_t81);
	const W w_a28 = An(w_t80, w_t70);
	const W w_t85 = X(w_t82, w_t42);
	const W w_S4 = X(w_a21, w_t83);
	const W w_S6 = X(w_a17, w_t84);
	const W w_t86 = X(w_t84, w_t42);
	const W w_t87 = X(w_a25, w_t85);
	const W w_t88 = X(w_S6, w_t68);
	const W w_t89 = X(w_a24, w_t86);
	const W w_t90 = X(w_t85, w_t71);
	const W w_t91 = X(w_S4, w_t88);
	const W w_t92 = X(w_t29, w_t89);
	const W w_t93 = X(w_a24, w_t90);
	const W w_S0 = X(w_t87, w_t91);
	const W w_t94 = X(w_a28, w_t91);
	const W w_S5 = X(w_t83, w_t93);
	const W w_t95 = X(w_a19, w_t93);
	const W w_S1 = X(w_a27, w_t94);
	const W w_S3 = X(w_t92, w_t94);
	const W w_S7 = X(w_t86, w_t95);
	const W n_S0 = Nt(w_S0);
	const W n_S1 = Nt(w_S1);
	const W n_S2 = Nt(w_S2);
	const W n_S3 = Nt(w_S3);
	const W n_S5 = Nt(w_S5);
	const W n_S6 = Nt(w_S6);
	const W n_S7 = Nt(w_S7);
	S_lsb[7] = n_S0;
	S_lsb[6] = n_S1;
	S_lsb[5] = n_S2;
	S_lsb[4] = n_S3;
	S_lsb[3] = w_S4;
	S_lsb[2] = n_S5;
	S_lsb[1] = n_S6;
	S_lsb[0] = n_S7;
}

// xtime: multiply by x in GF(2^8) with poly x^8+x^4+x^3+x+1.
// LSB-first: in[0]=bit 0, in[7]=bit 7. Pure XOR, no ANDs.
template <BooleanContext Ctx>
inline void aes_xtime(Ctx& ctx, const typename Ctx::Wire in[8], typename Ctx::Wire out[8]) {
	const typename Ctx::Wire m = in[7];
	out[0] = m;
	out[1] = ctx.xor_gate(in[0], m);
	out[2] = in[1];
	out[3] = ctx.xor_gate(in[2], m);
	out[4] = ctx.xor_gate(in[3], m);
	out[5] = in[4];
	out[6] = in[5];
	out[7] = in[6];
}

// AES-128 key expansion: 128-bit key -> 11 round keys (1408 bits total).
// expanded[r*128 .. (r+1)*128) holds round key r. 40 SBox calls.
template <BooleanContext Ctx>
inline void aes128_key_schedule(Ctx& ctx, const typename Ctx::Wire key[128],
                                typename Ctx::Wire expanded[1408]) {
	using W = typename Ctx::Wire;

	for (int i = 0; i < 128; ++i) expanded[i] = key[i];

	static const unsigned char Rcon[11] = {
		0x00, 0x01, 0x02, 0x04, 0x08, 0x10,
		0x20, 0x40, 0x80, 0x1b, 0x36
	};

	for (int r = 1; r <= 10; ++r) {
		W *prev = expanded + (r - 1) * 128;
		W *cur  = expanded + r * 128;

		W tmp[32];
		// RotWord on bytes 12..15 of prev: [b12,b13,b14,b15] -> [b13,b14,b15,b12]
		for (int b = 0; b < 4; ++b) {
			int src_byte = 12 + ((b + 1) & 3);
			for (int k = 0; k < 8; ++k)
				tmp[b * 8 + k] = prev[src_byte * 8 + k];
		}
		// SubWord: SBox each of the 4 bytes.
		for (int b = 0; b < 4; ++b) {
			W in[8], out[8];
			for (int k = 0; k < 8; ++k) in[k] = tmp[b * 8 + k];
			aes_sbox<Ctx>(ctx, in, out);
			for (int k = 0; k < 8; ++k) tmp[b * 8 + k] = out[k];
		}
		// XOR Rcon[r] into byte 0 of tmp (LSB-first bit indexing).
		for (int k = 0; k < 8; ++k)
			if ((Rcon[r] >> k) & 1) tmp[k] = ctx.not_gate(tmp[k]);

		// Word 0 of cur = word 0 of prev XOR tmp
		for (int k = 0; k < 32; ++k) cur[k] = ctx.xor_gate(prev[k], tmp[k]);
		// Words 1..3 of cur = word w-1 of cur XOR word w of prev
		for (int w = 1; w < 4; ++w)
			for (int k = 0; k < 32; ++k)
				cur[w * 32 + k] = ctx.xor_gate(cur[(w - 1) * 32 + k], prev[w * 32 + k]);
	}
}

// AES-128 encrypt with an already-expanded key. 160 SBox calls.
template <BooleanContext Ctx>
inline void aes128_encrypt_block(Ctx& ctx, const typename Ctx::Wire plaintext[128],
                                 const typename Ctx::Wire expanded_key[1408],
                                 typename Ctx::Wire ciphertext[128]) {
	using W = typename Ctx::Wire;
	W state[128];
	for (int i = 0; i < 128; ++i) state[i] = ctx.xor_gate(plaintext[i], expanded_key[i]);

	for (int r = 1; r <= 10; ++r) {
		// SubBytes: 16 SBoxes
		for (int b = 0; b < 16; ++b) {
			W in[8], out[8];
			for (int k = 0; k < 8; ++k) in[k] = state[b * 8 + k];
			aes_sbox<Ctx>(ctx, in, out);
			for (int k = 0; k < 8; ++k) state[b * 8 + k] = out[k];
		}

		// ShiftRows: new state[c, row] = old state[(c+row) mod 4, row]
		W shifted[128];
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
			W mixed[128];
			for (int c = 0; c < 4; ++c) {
				const W *col = state + c * 32;
				const W *a = col + 0;
				const W *b = col + 8;
				const W *cc = col + 16;
				const W *d = col + 24;

				W t[8], ab[8], bc[8], cd[8], da[8];
				W xab[8], xbc[8], xcd[8], xda[8];
				for (int k = 0; k < 8; ++k) {
					t[k]  = ctx.xor_gate(ctx.xor_gate(a[k], b[k]), ctx.xor_gate(cc[k], d[k]));
					ab[k] = ctx.xor_gate(a[k], b[k]);
					bc[k] = ctx.xor_gate(b[k], cc[k]);
					cd[k] = ctx.xor_gate(cc[k], d[k]);
					da[k] = ctx.xor_gate(d[k], a[k]);
				}
				aes_xtime<Ctx>(ctx, ab, xab);
				aes_xtime<Ctx>(ctx, bc, xbc);
				aes_xtime<Ctx>(ctx, cd, xcd);
				aes_xtime<Ctx>(ctx, da, xda);

				W *na = mixed + c * 32 + 0;
				W *nb = mixed + c * 32 + 8;
				W *nc = mixed + c * 32 + 16;
				W *nd = mixed + c * 32 + 24;
				for (int k = 0; k < 8; ++k) {
					na[k] = ctx.xor_gate(a[k],  ctx.xor_gate(t[k], xab[k]));
					nb[k] = ctx.xor_gate(b[k],  ctx.xor_gate(t[k], xbc[k]));
					nc[k] = ctx.xor_gate(cc[k], ctx.xor_gate(t[k], xcd[k]));
					nd[k] = ctx.xor_gate(d[k],  ctx.xor_gate(t[k], xda[k]));
				}
			}
			for (int i = 0; i < 128; ++i) state[i] = mixed[i];
		}

		// AddRoundKey r
		const W *rk = expanded_key + r * 128;
		for (int i = 0; i < 128; ++i) state[i] = ctx.xor_gate(state[i], rk[i]);
	}

	for (int i = 0; i < 128; ++i) ciphertext[i] = state[i];
}

// Convenience: key schedule + block encrypt in one call.
template <BooleanContext Ctx>
inline void aes128_encrypt(Ctx& ctx, const typename Ctx::Wire plaintext[128],
                           const typename Ctx::Wire key[128], typename Ctx::Wire ciphertext[128]) {
	typename Ctx::Wire expanded[1408];
	aes128_key_schedule<Ctx>(ctx, key, expanded);
	aes128_encrypt_block<Ctx>(ctx, plaintext, expanded, ciphertext);
}

// Add `delta` to the big-endian 64-bit counter in bytes 8..15 of a 128-bit CTR
// block (NIST SP 800-38A: byte 15 is the LSB; carry beyond byte 8 is dropped).
template <BooleanContext Ctx>
inline void ctr_inc_be64(Ctx& ctx, typename Ctx::Wire block[128], uint64_t delta) {
	if (delta == 0) return;
	UInt_T<Ctx, 64> ctr(ctx);
	for (int byte = 0; byte < 8; ++byte) {            // pack bytes 15,14,...,8 LSB-first
		int src = 15 - byte;
		for (int k = 0; k < 8; ++k) ctr.w[byte * 8 + k] = block[src * 8 + k];
	}
	ctr = ctr + UInt_T<Ctx, 64>::constant(ctx, delta);
	for (int byte = 0; byte < 8; ++byte) {
		int dst = 15 - byte;
		for (int k = 0; k < 8; ++k) block[dst * 8 + k] = ctr.w[byte * 8 + k];
	}
}

// AES-128-CTR (NIST SP 800-38A): keystream block i = AES(key, counter_i), out =
// in ^ keystream, for length_bits bits (a partial final block is allowed). The
// 128-bit `iv` is the initial counter block; bytes 8..15 are the big-endian
// counter incremented per block. `start_chunk` offsets the initial counter.
template <BooleanContext Ctx>
inline void aes128_ctr(Ctx& ctx, const typename Ctx::Wire key[128], const typename Ctx::Wire iv[128],
                       const typename Ctx::Wire* in, typename Ctx::Wire* out, std::size_t length_bits,
                       uint64_t start_chunk = 0) {
	using W = typename Ctx::Wire;
	W round_keys[1408];
	aes128_key_schedule<Ctx>(ctx, key, round_keys);
	W counter[128];
	for (int i = 0; i < 128; ++i) counter[i] = iv[i];
	ctr_inc_be64<Ctx>(ctx, counter, start_chunk);
	for (std::size_t done = 0; done < length_bits; ) {
		W ks[128];
		aes128_encrypt_block<Ctx>(ctx, counter, round_keys, ks);
		std::size_t blk = std::min<std::size_t>(128, length_bits - done);
		for (std::size_t i = 0; i < blk; ++i) out[done + i] = ctx.xor_gate(in[done + i], ks[i]);
		done += blk;
		if (done < length_bits) ctr_inc_be64<Ctx>(ctx, counter, 1);   // no increment past the last block
	}
}

}  // namespace detail

template <BooleanContext Ctx>
inline BitVec_T<Ctx, 8> aes_sbox(Ctx& ctx, const BitVec_T<Ctx, 8>& byte) {
	BitVec_T<Ctx, 8> out(ctx);
	detail::aes_sbox<Ctx>(ctx, byte.w.data(), out.w.data());
	return out;
}

template <BooleanContext Ctx>
inline BitVec_T<Ctx, 8> aes_xtime(Ctx& ctx, const BitVec_T<Ctx, 8>& byte) {
	BitVec_T<Ctx, 8> out(ctx);
	detail::aes_xtime<Ctx>(ctx, byte.w.data(), out.w.data());
	return out;
}

template <BooleanContext Ctx>
inline BitVec_T<Ctx, 1408> aes128_key_schedule(Ctx& ctx, const BitVec_T<Ctx, 128>& key) {
	BitVec_T<Ctx, 1408> expanded(ctx);
	detail::aes128_key_schedule<Ctx>(ctx, key.w.data(), expanded.w.data());
	return expanded;
}

template <BooleanContext Ctx>
inline BitVec_T<Ctx, 128> aes128_encrypt_block(Ctx& ctx, const BitVec_T<Ctx, 128>& plaintext,
                                               const BitVec_T<Ctx, 1408>& expanded_key) {
	BitVec_T<Ctx, 128> ciphertext(ctx);
	detail::aes128_encrypt_block<Ctx>(ctx, plaintext.w.data(), expanded_key.w.data(), ciphertext.w.data());
	return ciphertext;
}

template <BooleanContext Ctx>
inline BitVec_T<Ctx, 128> aes128_encrypt(Ctx& ctx, const BitVec_T<Ctx, 128>& plaintext,
                                         const BitVec_T<Ctx, 128>& key) {
	BitVec_T<Ctx, 128> ciphertext(ctx);
	detail::aes128_encrypt<Ctx>(ctx, plaintext.w.data(), key.w.data(), ciphertext.w.data());
	return ciphertext;
}

template <BooleanContext Ctx>
inline BitVec_T<Ctx, 128> ctr_inc_be64(Ctx& ctx, const BitVec_T<Ctx, 128>& block, uint64_t delta) {
	BitVec_T<Ctx, 128> out(block);
	detail::ctr_inc_be64<Ctx>(ctx, out.w.data(), delta);
	return out;
}

template <BooleanContext Ctx, int N>
inline BitVec_T<Ctx, N> aes128_ctr(Ctx& ctx, const BitVec_T<Ctx, 128>& key,
                                   const BitVec_T<Ctx, 128>& iv,
                                   const BitVec_T<Ctx, N>& in,
                                   uint64_t start_chunk = 0) {
	static_assert(N >= 0, "aes128_ctr<N>: bit length must be non-negative");
	BitVec_T<Ctx, N> out(ctx);
	detail::aes128_ctr<Ctx>(ctx, key.w.data(), iv.w.data(), in.w.data(), out.w.data(),
	                        static_cast<std::size_t>(N), start_chunk);
	return out;
}

}  // namespace crypto
}  // namespace circuit
}  // namespace emp

#endif  // EMP_CIRCUIT_CRYPTO_AES128_H__
