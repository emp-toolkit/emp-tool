#include <typeinfo>
#include "plain_env.h"
#include "float.h"
#include <iostream>
using namespace std;

void test_float(int party) {
	Float a(24, 9, 0.11);
	Float b(24, 9, 0.41);
	cout << a.reveal<double>(PUBLIC)<<endl;
	cout << b.reveal<double>(PUBLIC)<<endl;
	cout << (a+b).reveal<double>(PUBLIC)<<endl;
	cout << (a-b).reveal<double>(PUBLIC)<<endl;
	cout << (a*b).reveal<double>(PUBLIC)<<endl;
	double res = (a/b).reveal<double>(BOB);
	cout << res <<endl;
}

int main(int argc, char** argv) {
	int party = PUBLIC;
	setup_plain_env(false, "");
	test_float(party);
	finalize_plain_env();
	return 0;
}
