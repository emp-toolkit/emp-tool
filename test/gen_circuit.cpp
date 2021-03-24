#include "emp-tool/emp-tool.h"
using namespace emp;

struct testMe {
  uint8_t t;
  uint32_t z;
  uint64_t x;
  uint64_t y;
};

void ham(int n) {
	Integer a(n, 0, ALICE);
	Integer b(n, 0, BOB);
	Integer c = a^b;
	Integer d = c.hamming_weight();
	d.reveal<string>();
}

void mult(int n) {
	Integer a(n, 0, ALICE);
	Integer b(n, 0, BOB);
	Integer c = a*b;
	c.reveal<string>();
}
void modexp(int n1, int n2) {
	Integer a(n1, 0,  ALICE);
	Integer b(n2, 0,  BOB);
	Integer c(n1, 5, ALICE);
	Integer d = a.modExp(b, c);
}
void sort(int n) {
	Integer *A = new Integer[n];
	Integer *B = new Integer[n];
	for(uint64_t i = 0; i < n; ++i) {
    struct testMe t;
    t.x = i;
    t.y = i + 1;
    t.z = (uint32_t) i << 20;
    t.t = (uint8_t) i;
		A[i] = Integer(32, &t, ALICE);
  }
	for(int i = 0; i < n; ++i) {
    struct testMe t;
    t.x = i + 10;
    t.y = i + 11;
    t.z = 0;
    t.t = (uint8_t) i;
		B[i] = Integer(32, &t, BOB);
  }
	for(int i = 0; i < n; ++i)
		A[i] = A[i] ^ B[i];
	sort(A, n);
	for(int i = 0; i < n; ++i)
		A[i].reveal<string>();
}
int main(int argc, char** argv) {
	setup_plain_prot(true, "sort.txt");
	sort(128);	
//	mult(2048);
//	ham(1<<10);
	finalize_plain_prot ();
}
