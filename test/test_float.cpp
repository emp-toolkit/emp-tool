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

template<typename Op, typename Op2>
void test_float(double precision, int runs = 1000) {
	PRG prg;
	for(int i = 0; i < runs; ++i) {
		int ia = 0, ib = 0;
		prg.random_data_unaligned(&ia, 4);
		prg.random_data_unaligned(&ib, 4);
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
		prg.random_data_unaligned(&ia, sizeof(long));
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

	Float z = If(one).Then(y).Else(x);
	cout << z.reveal<string>() << " ";
	z = If(zero).Then(y).Else(x);
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

// Round-trip int → float → int at the given scale with random magnitudes
// that fit in 24 bits (Float's significand precision). Bit-exact.
void test_float_int_roundtrip_small(size_t scale, int runs = 1000) {
	PRG prg;
	int rate_cnt = 0;
	for (int i = 0; i < runs; ++i) {
		int64_t raw;
		prg.random_data_unaligned(&raw, sizeof(raw));
		// Sign-extend the top 24 bits to int64 → range [-2^23, 2^23).
		int64_t v = raw >> 40;

		// signed
		SignedInt_T<block, 64> si(v, PUBLIC);
		Float f1 = si.to_float32(scale);
		int64_t back_s = f1.to_signed<64>(scale).reveal<int64_t>(PUBLIC);
		if (back_s != v) {
			cout << "Inaccuracy (signed s=" << scale << "): "
			     << v << " -> " << back_s << endl;
			rate_cnt++;
		}

		// unsigned: feed |v|
		uint64_t uv = (uint64_t)(v < 0 ? -v : v);
		UnsignedInt_T<block, 64> ui(uv, PUBLIC);
		Float f2 = ui.to_float32(scale);
		uint64_t back_u = f2.to_unsigned<64>(scale).reveal<uint64_t>(PUBLIC);
		if (back_u != uv) {
			cout << "Inaccuracy (unsigned s=" << scale << "): "
			     << uv << " -> " << back_u << endl;
			rate_cnt++;
		}
	}
	cout << "test_float_int_roundtrip_small s=" << scale
	     << "\t\tDONE  -  accuracy : "
	     << (1.0 - (float)rate_cnt / (2 * runs)) << endl;
}

// Larger magnitudes (up to ~52 bits) where the float's 24-bit significand
// truncates low bits. Check the round-trip is within float precision.
void test_float_int_roundtrip_lossy(size_t scale, int runs = 200) {
	PRG prg;
	int rate_cnt = 0;
	for (int i = 0; i < runs; ++i) {
		int64_t raw;
		prg.random_data_unaligned(&raw, sizeof(raw));
		int64_t v = raw >> 12;     // 52-bit signed range

		SignedInt_T<block, 64> si(v, PUBLIC);
		Float f1 = si.to_float32(scale);
		int64_t back = f1.to_signed<64>(scale).reveal<int64_t>(PUBLIC);

		// Float keeps 24 significant bits; allow up to 2^(53-24) = 2^29
		// of relative error in the worst case, which is the trailing
		// bits the float can't represent.
		int64_t diff = back - v;
		double rel = std::fabs((double)diff / (v == 0 ? 1.0 : (double)v));
		if (rel > 1e-6) {
			cout << "Inaccuracy (lossy s=" << scale << "): "
			     << v << " -> " << back << " (rel " << rel << ")" << endl;
			rate_cnt++;
		}
	}
	cout << "test_float_int_roundtrip_lossy s=" << scale
	     << "\t\tDONE  -  accuracy : "
	     << (1.0 - (float)rate_cnt / runs) << endl;
}

void test_float_int_edges() {
	cout << "Edge cases:" << endl;

	// Zero in both directions.
	SignedInt_T<block, 32> z_int(0, PUBLIC);
	double zf = z_int.to_float32(0).reveal<double>(PUBLIC);
	cout << "  int 0 -> float : " << zf << " (expect 0)" << endl;

	Float zfloat(0.0f, PUBLIC);
	int32_t zb = zfloat.to_signed<32>(0).reveal<int32_t>(PUBLIC);
	cout << "  float 0.0 -> int : " << zb << " (expect 0)" << endl;

	// Sign-flip
	SignedInt_T<block, 32> sp(7, PUBLIC), sn(-7, PUBLIC);
	cout << "   7 -> float : " << sp.to_float32(0).reveal<double>(PUBLIC)
	     << " (expect 7)" << endl;
	cout << "  -7 -> float : " << sn.to_float32(0).reveal<double>(PUBLIC)
	     << " (expect -7)" << endl;

	// Cross-check known values.
	Float onehalf(1.5f, PUBLIC);
	cout << "  1.5  (s=1) -> int : "
	     << onehalf.to_signed<32>(1).reveal<int32_t>(PUBLIC)
	     << " (expect 3)" << endl;
	Float quarter(0.25f, PUBLIC);
	cout << "  0.25 (s=4) -> int : "
	     << quarter.to_signed<32>(4).reveal<int32_t>(PUBLIC)
	     << " (expect 4)" << endl;
	Float negthree(-3.0f, PUBLIC);
	cout << " -3.0 (s=0) -> int : "
	     << negthree.to_signed<32>(0).reveal<int32_t>(PUBLIC)
	     << " (expect -3)" << endl;

	// Verify SignedInt::to_float32 matches -(unsigned magnitude):
	SignedInt_T<block, 32> sp42(42, PUBLIC), sn42(-42, PUBLIC);
	float pf = sp42.to_float32(0).reveal<float>(PUBLIC);
	float nf = sn42.to_float32(0).reveal<float>(PUBLIC);
	cout << "  42 = " << pf << ",  -42 = " << nf
	     << " (expect 42 and -42)" << endl;
}

int main(int argc, char** argv) {
	setup_clear_backend();

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

	cout << endl << "Test float <-> int conversion:" << endl;
	test_float_int_edges();
	test_float_int_roundtrip_small(0);
	test_float_int_roundtrip_small(1);
	test_float_int_roundtrip_small(10);
	test_float_int_roundtrip_small(20);
	test_float_int_roundtrip_lossy(0);
	test_float_int_roundtrip_lossy(10);

	finalize_clear_backend();
	return 0;
}
