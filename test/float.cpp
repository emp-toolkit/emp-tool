#include <typeinfo>
#include "emp-tool/emp-tool.h"
#include <iostream>
#include <cmath>
#include <vector>
using namespace std;
using namespace emp;

vector<std::string> test_str{"sqr", "sqrt", "sin", "cos", "exp2", "exp", "ln", "log2"};

void print_float32(Float a) {
	for(int i = 31; i >= 0; i--)
		printf("%d", a[i].reveal<bool>());
	cout << endl;
}

void print_float(float a) {
	unsigned char* c = (unsigned char*)&a;
	for(int i = 0; i < 4; i++)
		printf("%X", c[i]);
	cout << endl;
}

bool equal(Float a, float b) {
	unsigned char pa[sizeof(float)];
	memset(pa, 0, sizeof(float));
	unsigned char *pb = (unsigned char*)(&b);
	for(int i = 0; i < (int)sizeof(float); i++) {
		for(int j = 0; j < 8; j++) {
			pa[i] += (a[i*8+j].reveal<bool>())<<j;
		}
	}
	if(memcmp(pa, pb, sizeof(float)) == 0)
		return true;
	else return false;
}

bool accurate(double a, double b, double err) {
	if (fabs(a - b) < fabs(err*a) and fabs(a - b) < fabs(err*b))
		return true;
	else return false;
}

PRG prg;
template<typename Op, typename Op2>
void test_float(double precision, int runs = 1000) {
	for(int i = 0; i < runs; ++i) {
		int ia = 0, ib = 0;
		prg.random_data(&ia, 4);
		prg.random_data(&ib, 4);
		float da = (float)(ia) / 10000000.0;
		float db = (float)(ib) / 10000000.0;

		Float a(da, PUBLIC);
		Float b(db, PUBLIC);
		Float res = Op2()(a,b);

		if(precision > 0.0) {
			if (not accurate(res.reveal<double>(PUBLIC), Op()(da,db), precision)) {
				cout << "Inaccuracy:\t"<<typeid(Op2).name()<<"\t"<< da <<"\t"<<db<<"\t"<<Op()(da,db)<<"\t"<<res.reveal<double>(PUBLIC)<<endl;
			}
		} else {
			if (not equal(res, Op()(da,db))) {
				cout << "Inaccuracy:\t"<<typeid(Op2).name()<<"\t"<< da <<"\t"<<db<<"\t"<<Op()(da,db)<<"\t"<<res.reveal<double>(PUBLIC)<<endl;
			}
		}
	}
	cout << typeid(Op2).name()<<"\t\t\tDONE"<<endl;
}

void test_float(int func_id, double precision, double minimize, int runs = 1000) {
	PRG prg;
	int rate_cnt = 0;
	const double pi = std::acos(-1);
	for(int i = 0; i < runs; ++i) {
		long ia;
		prg.random_data(&ia, sizeof(long));
		float da = ia / minimize;
		Float a(da, PUBLIC);
		Float res = Float(0.0, PUBLIC);
		float comp = 0.0;
		switch(func_id) {
			case 3: res = a.cos();
				comp = std::cos(da*pi);
				break;
			case 0: res = a.sqr();
				comp = std::pow(da, 2.0);
				break;
			case 1: res = a.abs().sqrt();
				comp = std::sqrt(da>0?da:(-da));
				break;
			case 2: res = a.sin();
				comp = std::sin(da*pi);
				break;
			case 4: res = a.exp2();
				comp = std::exp2(da);
				break;
			case 5: res = a.exp();
				comp = std::exp(da);
				break;
			case 6: res = a.abs().ln();
				comp = std::log(da>0?da:(-da));
				break;
			case 7: res = a.abs().log2();
				comp = std::log2(da>0?da:(-da));
				break;
		}
		if(precision == 0.0) {
			if (not equal(res, comp)) {
				cout << "Inaccuracy:\t"<<da<<"\t"<<"\t"<<comp<<"\t"<<res.reveal<double>(PUBLIC)<<endl;
				rate_cnt++;
			}
		} else {
			if (not accurate(res.reveal<double>(PUBLIC), comp, precision)) {
				cout << "Inaccuracy:\t"<<da<<"\t"<<"\t"<<comp<<"\t"<<res.reveal<double>(PUBLIC)<<endl;
				rate_cnt++;
			}
		}
	}
	cout << "function " << test_str[func_id] <<"\t\t\tDONE"<<"  -  accuracy : "<<(1.0-(float)rate_cnt/runs)<<endl;
}

void scratch_pad(double num) {
	cout << "input: " << num << endl;
	Float x(num, PUBLIC);

	cout << "ultimate: ";
	for(int i = x.size()-1; i >= 0; i--) {
		cout << x.value[i].reveal<bool>(PUBLIC);
	}
	cout << endl;

	cout << "test reveal: ";
	cout << x.reveal<string>() << " or " << x.reveal<double>() << endl << endl;
}

void fp_cmp(double a, double b) {
	cout << "compare (eq, le, lt): " << a << " " << b << " - ";
	Float x(a, PUBLIC);
	Float y(b, PUBLIC);

	Bit z = x.equal(y);
	cout << z.reveal<bool>() << " ";
	z = x.less_equal(y);
	cout << z.reveal<bool>() << " ";
	z = x.less_than(y);
	cout << z.reveal<bool>() << endl;
}

void fp_if(double a, double b) {
	cout << "if true/false: " << a << " " << b << " - ";
	Float x(a, PUBLIC);
	Float y(b, PUBLIC);
	Bit one = Bit(true, PUBLIC);
	Bit zero = Bit(false, PUBLIC);

	Float z = x.If(one, y);
	cout << z.reveal<string>() << " ";
	z = x.If(zero, y);
	cout << z.reveal<string>() << endl<<endl;
	swap(Bit(true, PUBLIC), x, y);
	cout << x.reveal<string>() << " "<<y.reveal<string>();
}

void fp_abs(double a) {
	cout << "abs: " << a << " - ";
	Float x(a, PUBLIC);

	Float z = x.abs();
	cout << z.reveal<string>() << endl;
}

int main(int argc, char** argv) {
	setup_plain_prot(false, "");

	cout << "Test function:" << endl;
	fp_cmp(52.21875, 52.21875);
	fp_cmp(24.4332565, 52.21875);
	fp_if(24.4332565, 52.21875);
	fp_abs(-24.422432);
	fp_abs(24.422432);

	cout << endl << "Test accuracy:" << endl;
	test_float<std::plus<float>, std::plus<Float>>(0.0);
	test_float<std::minus<float>, std::minus<Float>>(0.0);
	test_float<std::multiplies<float>, std::multiplies<Float>>(0.0);
	test_float<std::divides<float>, std::divides<Float>>(0.0);
	test_float(0, 0.0, 1e12);
	test_float(1, 0.0, 1e12);
	test_float(2, 1e-3, 1e15);
	test_float(3, 1e-3, 1e15);
	test_float(4, 1e-3, 1e18);
	test_float(5, 1e-3, 1e18);
	test_float(6, 1e-3, 1e12);
	test_float(7, 1e-3, 1e12);

	finalize_plain_prot();
	return 0;
}
