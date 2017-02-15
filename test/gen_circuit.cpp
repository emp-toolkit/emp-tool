#include "circuit_generator.h"

void mult(int n) {
	Integer a(n, 0, ALICE);
	Integer b(n, 0, BOB);
	Integer c = a*b;
}
void modexp(int n1, int n2) {
	Integer a(n1, 0,  ALICE);
	Integer b(n2, 0,  BOB);
	Integer c(n1, 5, ALICE);
	Integer d = a.modExp(b, c);
}
void sort(int n) {
	Integer *A = new Integer[n];
	for(int i = 0; i < n; ++i)
		A[i] = Integer(32, 0, ALICE);
	sort(A, n);
}
int main(int argc, char** argv) {
	setup_circuit_generator(true);
//	sort(1024);	
	mult(128);
}
