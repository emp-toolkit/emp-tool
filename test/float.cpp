#include <typeinfo>
#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;
using namespace emp;

bool accurate(double a, double b, double err) {
	if (fabs(a - b) < err*a and fabs(a - b) < err*b)
		return true;
	else return false;
}
template<typename Op, typename Op2>
void test_float(double precision, int runs = 1000) {
	PRG prg;
	for(int i = 0; i < runs; ++i) {
		long long ia, ib;
		prg.random_data(&ia, 8);
		prg.random_data(&ib, 8);
		double da = ia / 1000.0;
		double db = ib / 1000.0;
		while( not accurate(Op()((float)da, (float)db),  Op()(da, db), precision )) {
			prg.random_data(&ia, 8);
			prg.random_data(&ib, 8);
			da = ia / 1000.0;
			db = ib / 1000.0;
		}
	
		Float a(24, 9, da, PUBLIC);
		Float b(24, 9, db, PUBLIC);
		Float res = Op2()(a,b);

		if (not accurate(res.reveal<double>(PUBLIC), Op()(da,db), precision)) {
			cout << "Inaccuracy:\t"<<typeid(Op2).name()<<"\t"<< da <<"\t"<<db<<"\t"<<Op()(da,db)<<"\t"<<res.reveal<double>(PUBLIC)<<endl;
		}
		assert(accurate(res.reveal<double>(PUBLIC),  Op()(da,db), precision*10));
	}
	cout << typeid(Op2).name()<<"\t\t\tDONE"<<endl;
}

void scratch_pad() {
	Float a(24, 9, 0.21, PUBLIC);
	Float b(24, 9, 0.41, PUBLIC);
	cout << a.reveal<double>(PUBLIC)<<endl;
	cout << b.reveal<double>(PUBLIC)<<endl;
	cout << (a+b).reveal<double>(PUBLIC)<<endl;
	cout << (a-b).reveal<double>(PUBLIC)<<endl;
	cout << (a*b).reveal<double>(PUBLIC)<<endl;
	double res = (a/b).reveal<double>(BOB);
	cout << res <<endl;
}

int main(int argc, char** argv) {
	setup_plain_prot(false, "");
//	scratch_pad();return 0;
	test_float<std::plus<float>, std::plus<Float>>(1e-3);
	test_float<std::minus<float>, std::minus<Float>>(1e-3);
	test_float<std::multiplies<float>, std::multiplies<Float>>(1e-3);
	test_float<std::divides<float>, std::divides<Float>>(1e-3);

	finalize_plain_prot();
	return 0;
}
