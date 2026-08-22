// crypto/ccrh.h — Circular CRH. H(x) = sigma(x) ^ PRP_K(sigma(x)). Reduces to
// CRH applied to sigma(x) = x ^ rotate_halves(x). Read example() first; the
// rest is verification.
//
// What's in ccrh.h:
//   CCRH(key=zero_block)       inherits PRP
//   block H(block)             single-block H
//   template<int n> H(out, in) batched
//   Hn(out, in, n)             runtime-sized, out-of-place (no overlap)
//   Hn(data, n)                runtime-sized, in-place

#include "emp-tool/emp-tool.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace emp;
using namespace std;
using clk = chrono::high_resolution_clock;

template <class F>
static bool dies(F&& f) {
	pid_t pid = fork();
	if (pid == 0) {
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) dup2(devnull, STDERR_FILENO);
		f();
		_exit(0);
	}
	int st = 0;
	waitpid(pid, &st, 0);
	return !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

static void example() {
	cout << "=== example ===\n";
	CCRH ccrh;  // zero key
	block in = makeBlock(0xCAFEBABEULL, 0xDEADBEEFULL);
	cout << "  H(in)               = " << ccrh.H(in) << "\n";

	block buf[4] = {
		makeBlock(0, 1), makeBlock(0, 2), makeBlock(0, 3), makeBlock(0, 4)};
	block out[4];
	ccrh.H<4>(out, buf);
	cout << "  H<4>(out, in)[0]    = " << out[0] << "\n";
}

static bool check_definition() {
	// H(x) must equal sigma(x) ^ AES_K(sigma(x)).
	PRG prg;
	for (int t = 0; t < 64; ++t) {
		block key, x;
		prg.random_block(&key, 1);
		prg.random_block(&x, 1);
		CCRH ccrh(key);
		PRP prp(key);
		block s = sigma(x);
		block ref = s;
		prp.permute_block(&ref, 1);
		ref = ref ^ s;
		block h = ccrh.H(x);
		if (!cmpBlock(&h, &ref, 1)) return false;
	}
	return true;
}

template <int n>
static bool check_batched() {
	PRG prg;
	for (int t = 0; t < 8; ++t) {
		block key; prg.random_block(&key, 1);
		block in[n], out[n], ref[n];
		prg.random_block(in, n);
		CCRH ccrh(key);
		ccrh.template H<n>(out, in);
		PRP prp(key);
		for (int i = 0; i < n; ++i) ref[i] = sigma(in[i]);
		block enc[n];
		memcpy(enc, ref, sizeof(enc));
		prp.permute_block(enc, n);
		for (int i = 0; i < n; ++i) ref[i] = ref[i] ^ enc[i];
		if (!cmpBlock(out, ref, n)) return false;
	}
	return true;
}

static bool check_Hn(int n) {
	PRG prg;
	block key; prg.random_block(&key, 1);
	vector<block> in(n), out(n), ref(n);
	prg.random_block(in.data(), n);
	CCRH ccrh(key);
	ccrh.Hn(out.data(), in.data(), n);

	PRP prp(key);
	for (int i = 0; i < n; ++i) ref[i] = sigma(in[i]);
	vector<block> enc = ref;
	prp.permute_block(enc.data(), n);
	for (int i = 0; i < n; ++i) ref[i] = ref[i] ^ enc[i];

	if (!cmpBlock(out.data(), ref.data(), n)) return false;

	// In-place overload must match the out-of-place result.
	vector<block> inplace = in;
	ccrh.Hn(inplace.data(), n);
	return cmpBlock(inplace.data(), ref.data(), n);
}

static bool check_negative_lengths_rejected() {
	CCRH ccrh;
	block input = zero_block;
	block output = zero_block;
	return dies([&] { ccrh.Hn(&output, &input, -1); })
	    && dies([&] { ccrh.Hn(&output, -1); });
}

static bool check_zero_lengths_noop() {
	CCRH ccrh;
	ccrh.Hn(nullptr, nullptr, 0);
	ccrh.Hn(nullptr, 0);
	return true;
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	bool ok = true;
	bool a = check_definition();      ok &= a;
	cout << "  H(x) = sigma(x) ^ PRP_K(sigma(x))       " << (a ? "OK" : "FAIL") << "\n";
	bool b = check_batched<1>() && check_batched<2>() && check_batched<3>() && check_batched<4>()
	      && check_batched<7>() && check_batched<8>() && check_batched<15>() && check_batched<16>()
	      && check_batched<17>() && check_batched<32>() && check_batched<64>();
	ok &= b;
	cout << "  template H<n> matches reference         " << (b ? "OK" : "FAIL") << "\n";
	// Widths straddle the 16/8/4/2/1 tile boundaries to exercise every lane
	// and the new VAES-512 16-block tile.
	bool c = true;
	for (int n : {1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 33, 64, 65, 1024}) c &= check_Hn(n);
	ok &= c;
	cout << "  Hn(runtime) matches reference           " << (c ? "OK" : "FAIL") << "\n";
	bool d = check_negative_lengths_rejected(); ok &= d;
	cout << "  Hn rejects negative lengths             " << (d ? "OK" : "FAIL") << "\n";
	bool e = check_zero_lengths_noop(); ok &= e;
	cout << "  Hn accepts zero-length null views        " << (e ? "OK" : "FAIL") << "\n";
	return ok;
}

int main(int /*argc*/, char ** /*argv*/) {
	example();
	cout << "\n";
	if (!run_correctness()) {
		cerr << "CORRECTNESS FAILURE\n";
		return 1;
	}
	return 0;
}
