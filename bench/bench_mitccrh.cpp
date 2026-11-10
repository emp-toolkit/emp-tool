// crypto/mitccrh.h — Multi-Instance Tweakable CCRH (Guo–Katz–Wang–Weng–Yu,
// eprint 2019/1168). A bucketed-key Davies-Meyer AES hash used by half-gates
// garbling. Throughput benchmark.
//
// What's in mitccrh.h:
//   MITCCRH<BatchSize>                       start_point(), next_gid()
//   setS(s)                                  install S, restart the gid stream at 0
//   renew_ks(gid)                            schedule B keys from gid
//   hash<K, H>(blks)                         consume K lanes, hash K*H blocks:
//                                              y ^ AES_{S ^ (gid>>RS)}(y), y = x ^ T(gid)
//   hash_cir<K, H>(blks)                     same with y = sigma(x) ^ T(gid)

#include "emp-tool/emp-tool.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace emp;
using namespace std;
using clk = chrono::high_resolution_clock;

// ---------- throughput bench ----------

template <typename Fn>
static double run_for(double seconds, Fn &&fn, void *clob) {
	for (int i = 0; i < 32; ++i) {
		fn();
		asm volatile("" : "+m"(*(char *)clob) : : "memory");
	}
	int64_t iters = 64;
	while (true) {
		auto a = clk::now();
		for (int64_t i = 0; i < iters; ++i) {
			fn();
			asm volatile("" : "+m"(*(char *)clob) : : "memory");
		}
		double el = chrono::duration<double>(clk::now() - a).count();
		if (el >= seconds) return double(iters) / el;
		iters *= 2;
	}
}

static void print_vec(const string &lbl, double calls, int blocks_per_call) {
	double GiBps = calls * (double)blocks_per_call * 16.0 / (1024.0 * 1024.0 * 1024.0);
	double cy_per_blk = 3e9 / (calls * blocks_per_call);
	cout << "  " << left << setw(36) << lbl
	     << fixed << setprecision(2)
	     << right << setw(8) << GiBps << " GiB/s "
	     << setw(7) << cy_per_blk << " cy/blk @3GHz\n";
}

template <int K, int H, int RS = 3>
static double bench_hash(double sec) {
	static_assert(K <= 8 && 8 % K == 0, "MITCCRH<8> requires K | 8");
	MITCCRH<8, RS> mit;
	mit.setS(makeBlock(1, 2));
	mit.renew_ks(uint64_t{0});
	block buf[K * H];
	PRG().random_block(buf, K * H);
	return run_for(sec, [&]() { mit.template hash<K, H>(buf); }, &buf[0]);
}

template <int K, int H, int RS = 3>
static double bench_hash_cir(double sec, uint64_t initial_gid = 0) {
	static_assert(K <= 8 && 8 % K == 0, "MITCCRH<8> requires K | 8");
	MITCCRH<8, RS> mit;
	mit.setS(makeBlock(1, 2));
	mit.renew_ks(initial_gid);
	block buf[K * H];
	PRG().random_block(buf, K * H);
	return run_for(sec, [&]() { mit.template hash_cir<K, H>(buf); }, &buf[0]);
}

template <int K, int H, int RS = 3>
static double bench_hash_cir_out_of_place(double sec, uint64_t initial_gid = 0) {
	static_assert(K <= 8 && 8 % K == 0, "MITCCRH<8> requires K | 8");
	MITCCRH<8, RS> mit;
	mit.setS(makeBlock(1, 2));
	mit.renew_ks(initial_gid);
	block in[K * H], out[K * H];
	PRG().random_block(in, K * H);
	return run_for(sec, [&]() { mit.template hash_cir<K, H>(out, in); }, &out[0]);
}

// The rows above hash one buffer in place, so each call's input is the
// previous call's output. A half-gate caller instead writes each call's input
// block by block from labels it has just read; these rows do the same, drawing
// every block from a different slot of a table that stays in cache.
template <int K, int H, bool Cir>
static double bench_gathered(double sec) {
	static_assert(K <= 8 && 8 % K == 0, "MITCCRH<8> requires K | 8");
	constexpr int N = K * H;
	constexpr uint64_t kLabels = 1 << 12;
	vector<block> labels(kLabels);
	PRG().random_block(labels.data(), (int64_t)kLabels);
	MITCCRH<8> mit;
	mit.setS(makeBlock(1, 2));
	mit.renew_ks(uint64_t{0});
	block buf[N], sink = zero_block;
	uint64_t call = 0;
	return run_for(sec, [&]() {
		for (int i = 0; i < N; ++i)
			buf[i] = labels[(call * N + (uint64_t)i * 1031) & (kLabels - 1)];
		++call;
		if constexpr (Cir) mit.template hash_cir<K, H>(buf);
		else mit.template hash<K, H>(buf);
		for (int i = 0; i < N; ++i) sink = sink ^ buf[i];
	}, &sink);
}

static void bench(double sec) {
#if EMP_HAS_VAES512
	cout << "AES SIMD tier: VAES-512 (4 blocks/vector)\n\n";
#elif EMP_HAS_VAES256
	cout << "AES SIMD tier: VAES-256 (2 blocks/vector)\n\n";
#elif defined(__aarch64__)
	cout << "AES SIMD tier: ARMv8 Crypto (1 block/vector)\n\n";
#else
	cout << "AES SIMD tier: AES-NI (1 block/vector)\n\n";
#endif

	cout << "=== MITCCRH<8>::hash<K, H>  (default ReuseShift=3: schedule once per 8-gid bucket) ===\n";
	print_vec("hash<1,1>", bench_hash<1, 1>(sec), 1);
	print_vec("hash<1,4>", bench_hash<1, 4>(sec), 4);
	print_vec("hash<1,8>", bench_hash<1, 8>(sec), 8);
	print_vec("hash<2,1>", bench_hash<2, 1>(sec), 2);
	print_vec("hash<2,4>", bench_hash<2, 4>(sec), 8);
	print_vec("hash<4,1>", bench_hash<4, 1>(sec), 4);
	print_vec("hash<4,4>", bench_hash<4, 4>(sec), 16);
	print_vec("hash<8,1>", bench_hash<8, 1>(sec), 8);
	print_vec("hash<8,2>", bench_hash<8, 2>(sec), 16);
	print_vec("hash<8,4>", bench_hash<8, 4>(sec), 32);
	print_vec("hash<8,8>", bench_hash<8, 8>(sec), 64);

	cout << "\n=== MITCCRH<8, 0>  (one key per gid, no tweak reuse) ===\n";
	print_vec("hash<2,4>  shift0", bench_hash<2, 4, 0>(sec), 8);
	print_vec("hash<8,2>  shift0", bench_hash<8, 2, 0>(sec), 16);
	print_vec("hash<8,8>  shift0", bench_hash<8, 8, 0>(sec), 64);
	print_vec("hash_cir<2,1> shift0", bench_hash_cir<2, 1, 0>(sec), 2);
	print_vec("hash_cir<2,2> shift0", bench_hash_cir<2, 2, 0>(sec), 4);

	cout << "\n=== MITCCRH<8>::hash_cir<K, H>  (sigma + hash) ===\n";
	print_vec("hash_cir<1,1>", bench_hash_cir<1, 1>(sec), 1);
	print_vec("hash_cir<2,1>", bench_hash_cir<2, 1>(sec), 2);
	print_vec("hash_cir<2,2>", bench_hash_cir<2, 2>(sec), 4);
	print_vec("hash_cir<8,1>", bench_hash_cir<8, 1>(sec), 8);
	print_vec("hash_cir<8,2>", bench_hash_cir<8, 2>(sec), 16);

	cout << "\n=== hash_cir<2,2> call shape and gid alignment ===\n";
	print_vec("in-place, gid=0", bench_hash_cir<2, 2>(sec, 0), 4);
	print_vec("out-of-place, gid=0", bench_hash_cir_out_of_place<2, 2>(sec, 0), 4);
	print_vec("in-place, gid=1", bench_hash_cir<2, 2>(sec, 1), 4);

	cout << "\n=== input written block by block before each call ===\n";
	print_vec("hash_cir<2,2> gathered", bench_gathered<2, 2, true>(sec), 4);
	print_vec("hash_cir<2,1> gathered", bench_gathered<2, 1, true>(sec), 2);
	print_vec("hash_cir<1,4> gathered", bench_gathered<1, 4, true>(sec), 4);
	print_vec("hash_cir<8,2> gathered", bench_gathered<8, 2, true>(sec), 16);
	print_vec("hash<8,2> gathered", bench_gathered<8, 2, false>(sec), 16);
}

int main(int argc, char **argv) {
	double sec = (argc >= 2) ? atof(argv[1]) : 0.2;
	bench(sec);
	return 0;
}
