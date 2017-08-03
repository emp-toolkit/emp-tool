#include <typeinfo>
#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;

template<typename T, typename Op, typename Op2>
void test_int(int party, int range1 = 1<<25, int range2 = 1<<25, int runs = 100) {
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
	
		Integer<T> a(32, ia, ALICE); 
		Integer<T> b(32, ib, BOB);

		Integer<T> res = Op2()(a,b);

		if (res.reveal(PUBLIC) != Op()(ia,ib)) {
			cout << a.reveal(PUBLIC) <<"\t"<< b.reveal(PUBLIC)<<"\t"<<Op()(ia,ib)<<"\t"<<res.reveal(PUBLIC)<<endl<<flush;
		}
		assert(res.reveal(PUBLIC) == Op()(ia,ib));
	}
	cout << typeid(Op2).name()<<"\t\t\tDONE"<<endl;
}

template<typename T>
void scratch_pad() {
	Integer<T> a(32, 9, ALICE);
	cout << "HW "<<a.hamming_weight().reveal(PUBLIC)<<endl;
	cout << "LZ "<<a.leading_zeros().reveal(PUBLIC)<<endl;
	cout << T::circ_exec->gid<<endl;
}
int main(int argc, char** argv) {
	int party = PUBLIC;
	setup_plain_prot(false, "");

//	scratch_pad();return 0;
	test_int<PlainCircExec, std::plus<int>, std::plus<Integer<PlainCircExec>>>(party);
	test_int<PlainCircExec, std::minus<int>, std::minus<Integer<PlainCircExec>>>(party);
	test_int<PlainCircExec, std::multiplies<int>, std::multiplies<Integer<PlainCircExec>>>(party);
	test_int<PlainCircExec, std::divides<int>, std::divides<Integer<PlainCircExec>>>(party);
	test_int<PlainCircExec, std::modulus<int>, std::modulus<Integer<PlainCircExec>>>(party);

	test_int<PlainCircExec, std::bit_and<int>, std::bit_and<Integer<PlainCircExec>>>(party);
	test_int<PlainCircExec, std::bit_or<int>, std::bit_or<Integer<PlainCircExec>>>(party);
	test_int<PlainCircExec, std::bit_xor<int>, std::bit_xor<Integer<PlainCircExec>>>(party);
	finalize_plain_prot();
}
