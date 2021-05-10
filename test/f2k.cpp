#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;
using namespace emp;

void mul128_0(__m128i a, __m128i b, __m128i *res1, __m128i *res2) {
	__m128i tmp3, tmp4, tmp5, tmp6;
	tmp3 = _mm_clmulepi64_si128(a, b, 0x00);
	tmp4 = _mm_clmulepi64_si128(a, b, 0x10);
	tmp5 = _mm_clmulepi64_si128(a, b, 0x01);
	tmp6 = _mm_clmulepi64_si128(a, b, 0x11);

	tmp4 = _mm_xor_si128(tmp4, tmp5);
	tmp5 = _mm_slli_si128(tmp4, 8);
	tmp4 = _mm_srli_si128(tmp4, 8);
	tmp3 = _mm_xor_si128(tmp3, tmp5);
	tmp6 = _mm_xor_si128(tmp6, tmp4);
	// initial mul now in tmp3, tmp6
	*res1 = tmp3;
	*res2 = tmp6;
}

void mul128_1(__m128i a, __m128i b, __m128i *res1, __m128i *res2) {
	__m128i tmp3, tmp4, tmp5, tmp6;
	tmp3 = _mm_clmulepi64_si128(a, b, 0x00);
	tmp6 = _mm_clmulepi64_si128(a, b, 0x11);
	tmp4 = _mm_shuffle_epi32(a,78);
	tmp5 = _mm_shuffle_epi32(b,78);
	tmp4 = _mm_xor_si128(tmp4, a);
	tmp5 = _mm_xor_si128(tmp5, b);
	tmp4 = _mm_clmulepi64_si128(tmp4, tmp5, 0x00);
	tmp4 = _mm_xor_si128(tmp4, tmp3);
	tmp4 = _mm_xor_si128(tmp4, tmp6);
	tmp5 = _mm_slli_si128(tmp4, 8);
	tmp4 = _mm_srli_si128(tmp4, 8);
	*res1 = _mm_xor_si128(tmp3, tmp5);
	*res2 = _mm_xor_si128(tmp6, tmp4);
}

void gfmul_0(__m128i a, __m128i b, __m128i *res){
	__m128i tmp3, tmp4, tmp5, tmp6,
			  tmp7, tmp8, tmp9, tmp10, tmp11, tmp12;
	__m128i XMMMASK = _mm_setr_epi32(0xffffffff, 0x0, 0x0, 0x0);
	tmp3 = _mm_clmulepi64_si128(a, b, 0x00);
	tmp6 = _mm_clmulepi64_si128(a, b, 0x11);
	tmp4 = _mm_shuffle_epi32(a,78);
	tmp5 = _mm_shuffle_epi32(b,78);
	tmp4 = _mm_xor_si128(tmp4, a);
	tmp5 = _mm_xor_si128(tmp5, b);
	tmp4 = _mm_clmulepi64_si128(tmp4, tmp5, 0x00);
	tmp4 = _mm_xor_si128(tmp4, tmp3);
	tmp4 = _mm_xor_si128(tmp4, tmp6);
	tmp5 = _mm_slli_si128(tmp4, 8);
	tmp4 = _mm_srli_si128(tmp4, 8);
	tmp3 = _mm_xor_si128(tmp3, tmp5);
	tmp6 = _mm_xor_si128(tmp6, tmp4);
	tmp7 = _mm_srli_epi32(tmp6, 31);
	tmp8 = _mm_srli_epi32(tmp6, 30);
	tmp9 = _mm_srli_epi32(tmp6, 25);
	tmp7 = _mm_xor_si128(tmp7, tmp8);
	tmp7 = _mm_xor_si128(tmp7, tmp9);
	tmp8 = _mm_shuffle_epi32(tmp7, 147);

	tmp7 = _mm_and_si128(XMMMASK, tmp8);
	tmp8 = _mm_andnot_si128(XMMMASK, tmp8);
	tmp3 = _mm_xor_si128(tmp3, tmp8);
	tmp6 = _mm_xor_si128(tmp6, tmp7);
	tmp10 = _mm_slli_epi32(tmp6, 1);
	tmp3 = _mm_xor_si128(tmp3, tmp10);
	tmp11 = _mm_slli_epi32(tmp6, 2);
	tmp3 = _mm_xor_si128(tmp3, tmp11);
	tmp12 = _mm_slli_epi32(tmp6, 7);
	tmp3 = _mm_xor_si128(tmp3, tmp12);

	*res = _mm_xor_si128(tmp3, tmp6);
}
void gfmul_1(__m128i a, __m128i b, __m128i *res){
	__m128i tmp3, tmp6, tmp7, tmp8, tmp9, tmp10, tmp11, tmp12;
	__m128i XMMMASK = _mm_setr_epi32(0xffffffff, 0x0, 0x0, 0x0);
	mul128_0(a, b, &tmp3, &tmp6);
	tmp7 = _mm_srli_epi32(tmp6, 31);
	tmp8 = _mm_srli_epi32(tmp6, 30);
	tmp9 = _mm_srli_epi32(tmp6, 25);
	tmp7 = _mm_xor_si128(tmp7, tmp8);
	tmp7 = _mm_xor_si128(tmp7, tmp9);
	tmp8 = _mm_shuffle_epi32(tmp7, 147);

	tmp7 = _mm_and_si128(XMMMASK, tmp8);
	tmp8 = _mm_andnot_si128(XMMMASK, tmp8);
	tmp3 = _mm_xor_si128(tmp3, tmp8);
	tmp6 = _mm_xor_si128(tmp6, tmp7);
	tmp10 = _mm_slli_epi32(tmp6, 1);
	tmp3 = _mm_xor_si128(tmp3, tmp10);
	tmp11 = _mm_slli_epi32(tmp6, 2);
	tmp3 = _mm_xor_si128(tmp3, tmp11);
	tmp12 = _mm_slli_epi32(tmp6, 7);
	tmp3 = _mm_xor_si128(tmp3, tmp12);

	*res = _mm_xor_si128(tmp3, tmp6);
}

void uni_hash_coeff_gen_batch_1(block* coeff, block seed, int sz) {
	coeff[0] = seed;
	for(int i = 1; i < sz; i++) {
		gfmul(coeff[i-1], seed, coeff+i);
	}
}

void uni_hash_coeff_gen_batch_2(block* coeff, block seed, int sz) {
	coeff[0] = seed;
	block multiplier;
	gfmul(seed, seed, &multiplier);
	coeff[1] = multiplier;
	for(int i = 2; i < sz; i+=2) {
		gfmul(coeff[i-2], multiplier, coeff+i);
		gfmul(coeff[i-1], multiplier, coeff+i+1);
	}
}

void uni_hash_coeff_gen_batch_4(block* coeff, block seed, int sz) {
	coeff[0] = seed;
	gfmul(seed, seed, &coeff[1]);
	gfmul(coeff[1], seed, &coeff[2]);
	block multiplier;
	gfmul(coeff[2], seed, &multiplier);
	coeff[3] = multiplier;
	for(int i = 4; i < sz; i+=4) {
		gfmul(coeff[i-4], multiplier, &coeff[i]);
		gfmul(coeff[i-3], multiplier, &coeff[i+1]);
		gfmul(coeff[i-2], multiplier, &coeff[i+2]);
		gfmul(coeff[i-1], multiplier, &coeff[i+3]);
	}
}

#define HASH_BLOCK_SZ 128

void hash1(block *ret, block r, block *a, int sz) {
	PRG prg;
	prg.reseed(&r);
	block *chi = new block[sz];
	prg.random_block(chi, sz);
	block r2=zero_block, tmp = zero_block;
	int i = 0;
	for(; i < sz/HASH_BLOCK_SZ; ++i) {
		vector_inn_prdt_sum_red<HASH_BLOCK_SZ>(&tmp, a+i*HASH_BLOCK_SZ, chi+i*HASH_BLOCK_SZ);
		r2 = r2 ^ tmp;
	}
	if(sz % HASH_BLOCK_SZ != 0)
		vector_inn_prdt_sum_red(&tmp, a+i*HASH_BLOCK_SZ, chi+i*HASH_BLOCK_SZ, sz%128);
	*ret = r2 ^ tmp;
	delete[] chi;
}

void hash2(block *ret, block r, block *a, int sz) {
	block *coeff = new block[sz];
	int i = 0;
	for(; i < sz/HASH_BLOCK_SZ; ++i) {
		uni_hash_coeff_gen<HASH_BLOCK_SZ>(coeff+i*HASH_BLOCK_SZ, r);
	}
	if(sz % HASH_BLOCK_SZ != 0)
		uni_hash_coeff_gen(coeff+i*HASH_BLOCK_SZ, r, sz%HASH_BLOCK_SZ);
	i = 0;
	block r2=zero_block, tmp = zero_block;
	for(; i < sz/HASH_BLOCK_SZ; ++i) {
		vector_inn_prdt_sum_red<HASH_BLOCK_SZ>(&tmp, a+i*HASH_BLOCK_SZ, coeff+i*HASH_BLOCK_SZ);
		r2 = r2 ^ tmp;
	}
	if(sz % HASH_BLOCK_SZ != 0)
		vector_inn_prdt_sum_red(&tmp, a+i*HASH_BLOCK_SZ, coeff+i*HASH_BLOCK_SZ, sz%HASH_BLOCK_SZ);
	*ret = r2 ^ tmp;
	delete[] coeff;
}

int main() {
	PRG prg;
	block a, b;
	block res[4];
	int bench_size = 1024*1024;

	cout << "Simplified GF2^128 multiplication ... ";

	for(int i = 0; i < 65536; ++i) {
		prg.random_block(&a, 1);
		prg.random_block(&b, 1);
		mul128_0(a, b, res, res+1);
		mul128_1(a, b, res+2, res+3);
		if(cmpBlock(res, res+2, 2) == false) {
			cout << "incorrect multiplication" << endl;
			abort();
		}
	}
	cout << "correct" << endl;

	cout << "Benchmark: ";
	auto start = clock_start();
	for(int i = 0; i < bench_size; ++i) {
		mul128_0(res[0], res[1], res+2, res+3);
		mul128_0(res[2], res[3], res, res+1);
	}
	cout << bench_size*2/(time_from(start))*1e6 << " operations/second" << endl;

	cout << "Benchmark: ";
	start = clock_start();
	for(int i = 0; i < bench_size; ++i) {
		mul128_1(res[0], res[1], res+2, res+3);
		mul128_1(res[2], res[3], res, res+1);
	}
	cout << bench_size*2/(time_from(start))*1e6 << " operations/second" << endl;

	cout << "\nSimplified GF2^128 multiplication ... ";	

	for(int i = 0; i < 65536; ++i) {
		prg.random_block(&a, 1);
		prg.random_block(&b, 1);
		gfmul_0(a, b, res);
		gfmul_1(a, b, res+1);
		if(cmpBlock(res, res+1, 1) == false) {
			cout << "incorrect multiplication" << endl;
			abort();
		}
	}
	cout << "correct" << endl;

	cout << "Benchmark: ";
	start = clock_start();
	for(int i = 0; i < bench_size; ++i) {
		gfmul_0(res[0], res[1], res+2);
		gfmul_0(res[2], res[3], res);
	}
	cout << bench_size*2/(time_from(start))*1e6 << " operations/second" << endl;

	cout << "Benchmark: ";
	start = clock_start();
	for(int i = 0; i < bench_size; ++i) {
		gfmul_1(res[0], res[1], res+2);
		gfmul_1(res[2], res[3], res);
	}
	cout << bench_size*2/(time_from(start))*1e6 << " operations/second" << endl;

	cout << "\nVector inner product (with reduction)\n";
	block v0[128], v1[128];
	block r[2];
	start = clock_start();
	for(int i = 0; i < bench_size; ++i) {
		prg.random_block(v0, 128);
		prg.random_block(v1, 128);
		vector_inn_prdt_sum_red(r, v0, v1, 128);
	}
	cout << "Benchmark: PRG+MUL: " << bench_size/(time_from(start))*1e6 << " operations/second" << endl;

	cout << "\nVector inner product (without reduction)\n";
	start = clock_start();
	for(int i = 0; i < bench_size; ++i) {
		prg.random_block(v0, 128);
		prg.random_block(v1, 128);
		vector_inn_prdt_sum_no_red(r, v0, v1, 128);
	}
	cout << "Benchmark: PRG+MUL: " << bench_size/(time_from(start))*1e6 << " operations/second" << endl;

	cout << "\nUniversal hash coefficients (no batch)\n";
	block seed;
	block coefficient[128];
	start = clock_start();
	for(int i = 0; i < bench_size; ++i) {
		prg.random_block(&seed, 1);
		uni_hash_coeff_gen_batch_1(coefficient, seed, 128);
	}
	cout << "Benchmark: PRG+Coefficient generation: " << bench_size/(time_from(start))*1e6 << " operations/second" << endl;

	cout << "\nUniversal hash coefficients (batch 2)\n";
	start = clock_start();
	for(int i = 0; i < bench_size; ++i) {
		prg.random_block(&seed, 1);
		uni_hash_coeff_gen_batch_2(coefficient, seed, 128);
	}
	cout << "Benchmark: PRG+Coefficient generation: " << bench_size/(time_from(start))*1e6 << " operations/second" << endl;

	cout << "\nUniversal hash coefficients (batch 4)\n";
	start = clock_start();
	for(int i = 0; i < bench_size; ++i) {
		prg.random_block(&seed, 1);
		uni_hash_coeff_gen_batch_4(coefficient, seed, 128);
	}
	cout << "Benchmark: PRG+Coefficient generation: " << bench_size/(time_from(start))*1e6 << " operations/second" << endl;

	start = clock_start();
	for(int i = 0; i < bench_size; ++i) {
		prg.random_block(&seed, 1);
		uni_hash_coeff_gen(coefficient, seed, 126);
	}
	cout << "Benchmark: PRG+Coefficient generation: " << bench_size/(time_from(start))*1e6 << " operations/second" << endl;

	cout << "\nGalois field packing\n";
	start = clock_start();
	GaloisFieldPacking pack;
	block data[128], res1[128];
	for(int i = 0; i < bench_size; ++i) {
		prg.random_block(data, 128);
		pack.packing(res1, data);
	}
	cout << "Benchmark: PRG+packing: " << bench_size/(time_from(start))*1e6 << " operations/second" << endl;

	cout << "\nXOR of all elements in a vector\n";
	start = clock_start();
	for(int i = 0; i < bench_size; ++i) {
		prg.random_block(data, 128);
		vector_self_xor(res1, data, 128);
	}
	cout << "Benchmark: PRG+XOR: " << bench_size/(time_from(start))*1e6 << " operations/second" << endl;

	cout << "\nHash method 1: inner product with Chi vector (PRG)\n";
	start = clock_start();
	int hash_len = 1024*128;
	block *to_hash = new block[hash_len];
	block ret;
	for(int i = 0; i < 1024; ++i) {
		prg.random_block(r, 1);
		prg.random_block(to_hash, hash_len);
		hash1(&ret, r[0], to_hash, hash_len);
	}
	cout << "Benchmark: PRG+XOR: " << 1024/(time_from(start))*1e6 << " operations/second" << endl;

	cout << "\nHash method 2: almost universal hash\n";
	start = clock_start();
	for(int i = 0; i < 1024; ++i) {
		prg.random_block(r, 1);
		prg.random_block(to_hash, hash_len);
		hash2(&ret, r[0], to_hash, hash_len);
	}
	cout << "Benchmark: PRG+XOR: " << 1024/(time_from(start))*1e6 << " operations/second" << endl;
	delete[] to_hash;

	return 0;
}
