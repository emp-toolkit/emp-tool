#include "emp-tool/emp-tool.h"
using namespace emp;


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
	setup_clear_backend("sort.txt");
	sort_demo(128);
	finalize_clear_backend();
	BristolFormat bf("sort.txt");
	bf.to_file("sort_file.h", "sort");
}
