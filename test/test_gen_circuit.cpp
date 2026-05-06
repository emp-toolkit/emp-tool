#include "emp-tool/emp-tool.h"
#include <cstdio>
using namespace emp;

// Print a circuit that reveals one Alice bit and two public constants,
// then re-execute the printed Bristol circuit and check the outputs.
// Catches the case where ClearBackend.reveal() emits a gate-id of 0
// for a public constant, which becomes a stray reference to input
// wire 0 in the trailer XORs.
void const_reveal_roundtrip() {
	const char* fname = "const_reveal.txt";
	setup_clear_backend(fname);
	Bit a(false, ALICE);
	Bit one(true,  PUBLIC);
	Bit zero(false, PUBLIC);
	a.reveal();
	one.reveal();
	zero.reveal();
	finalize_clear_backend();

	BristolFormat bf(fname);
	if (bf.n1 != 1 || bf.n2 != 0 || bf.n3 != 3)
		error("const_reveal: unexpected header");

	for (bool a_val : {false, true}) {
		setup_clear_backend();
		std::vector<Bit> in1(bf.n1), in2(bf.n2), out(bf.n3);
		in1[0] = Bit(a_val, ALICE);
		bf.compute(out.data(), in1.data(), in2.data());
		bool got_a   = out[0].reveal();
		bool got_one = out[1].reveal();
		bool got_zer = out[2].reveal();
		finalize_clear_backend();
		if (got_a != a_val || got_one != true || got_zer != false) {
			std::cout << "a_in=" << a_val
			          << " got=" << got_a << ',' << got_one << ',' << got_zer
			          << " expected=" << a_val << ",1,0\n";
			error("const_reveal: printed circuit mishandles public constants");
		}
	}
	std::remove(fname);
	std::cout << "const_reveal_roundtrip: success\n";
}

void ham(int n) {
	UnsignedInt a(n, 0u, ALICE);
	UnsignedInt b(n, 0u, BOB);
	UnsignedInt c = a ^ b;
	UnsignedInt d = c.hamming_weight();
	d.reveal<std::string>();
}

void mult(int n) {
	UnsignedInt a(n, 0u, ALICE);
	UnsignedInt b(n, 0u, BOB);
	UnsignedInt c = a * b;
	c.reveal<std::string>();
}

void modexp(int n1, int n2) {
	UnsignedInt a(n1, 0u, ALICE);
	UnsignedInt b(n2, 0u, BOB);
	UnsignedInt c(n1, 5u, ALICE);
	UnsignedInt d = a.mod_exp(b, c);
	(void)d;
}

void sort_demo(int n) {
	UnsignedInt *A = new UnsignedInt[n];
	UnsignedInt *B = new UnsignedInt[n];
	for (int i = 0; i < n; ++i)
		A[i] = UnsignedInt(32, (uint32_t)(n - i), ALICE);
	for (int i = 0; i < n; ++i)
		B[i] = UnsignedInt(32, (uint32_t)i, BOB);
	for (int i = 0; i < n; ++i)
		A[i] = A[i] ^ B[i];
	sort(A, n);
	for (int i = 0; i < n; ++i)
		A[i].reveal<std::string>();
	delete[] A;
	delete[] B;
}

int main(int /*argc*/, char ** /*argv*/) {
	const_reveal_roundtrip();
	setup_clear_backend("sort.txt");
	sort_demo(128);
	finalize_clear_backend();
	BristolFormat bf("sort.txt");
	bf.to_file("sort_file.h", "sort");
}
