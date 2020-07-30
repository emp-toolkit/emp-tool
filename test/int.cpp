#include <typeinfo>
#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;
using namespace emp;

template<typename Op, typename Op2>
void test_int(int party, int range1 = 1<<25, int range2 = 1<<25, int runs = 1000) {
	PRG prg;
	for(int i = 0; i < runs; ++i) {
		long long ia, ib;
		prg.random_data(&ia, 8);
		prg.random_data(&ib, 8);
		ia %= range1;
		ib %= range2;
		while( Op()(int(ia), int(ib)) != Op()(ia, ib) ) {
			prg.random_data(&ia, 8);
			prg.random_data(&ib, 8);
			ia %= range1;
			ib %= range2;
		}
	
		Integer a(32, ia, ALICE); 
		Integer b(32, ib, BOB);

		Integer res = Op2()(a,b);

		if (res.reveal<int>(PUBLIC) != Op()(ia,ib)) {
			cout << ia <<"\t"<<ib<<"\t"<<Op()(ia,ib)<<"\t"<<res.reveal<int>(PUBLIC)<<endl;
		}
		assert(res.reveal<int>(PUBLIC) == Op()(ia,ib));
	}
	cout << typeid(Op2).name()<<"\t\t\tDONE"<<endl;
}

void scratch_pad() {
	Integer a(32, 9, ALICE);
	cout << "HW "<<a.hamming_weight().reveal<string>(PUBLIC)<<endl;
	cout << "LZ "<<a.leading_zeros().reveal<string>(PUBLIC)<<endl;
}
int main(int argc, char** argv) {
	int party = PUBLIC;
	setup_plain_prot(false, "");

//	scratch_pad();return 0;
	test_int<std::plus<int>, std::plus<Integer>>(party);
	test_int<std::minus<int>, std::minus<Integer>>(party);
	test_int<std::multiplies<int>, std::multiplies<Integer>>(party);
	test_int<std::divides<int>, std::divides<Integer>>(party);
	test_int<std::modulus<int>, std::modulus<Integer>>(party);

	test_int<std::bit_and<int>, std::bit_and<Integer>>(party);
	test_int<std::bit_or<int>, std::bit_or<Integer>>(party);
	test_int<std::bit_xor<int>, std::bit_xor<Integer>>(party);

	finalize_plain_prot();
}
