#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;

template<typename T>
void test_bit() {
	bool b[] = {true, false};
	int p[] = {PUBLIC, ALICE, BOB};

	for(int i = 0; i < 2; ++i)
		for(int j = 0; j < 3; ++j)
			for(int k = 0; k < 2; ++k)
				for (int l= 0; l < 3; ++l)  {
					{
						Bit<T> b1(b[i], p[j]);
						Bit<T> b2(b[k], p[l]);
						bool res = (b1&b2).reveal(PUBLIC);
						if(res != (b[i] and b[k])) {
							cout<<"AND" <<i<<" "<<j<<" "<<k<<" "<<l<<" "<<res<<endl;
							error("test bit error!");
						}
						res = (b1 & b1).reveal(PUBLIC);
						if (res != b[i]) {
							cout<<"AND" <<i<<" "<<j<<res<<endl;
							error("test bit error!");
						}

						res = (b1 & (!b1)).reveal(PUBLIC);
						if (res) {
							cout<<"AND" <<i<<" "<<j<<res<<endl;
							error("test bit error!");
						}

					}
					{
						Bit<T> b1(b[i], p[j]);
						Bit<T> b2(b[k], p[l]);
						bool res = (b1^b2).reveal(PUBLIC);
						if(res != (b[i] xor b[k])) {
							cout <<"XOR"<<i<<" "<<j<<" "<<k<<" "<<l<< " " <<res<<endl;
							error("test bit error!");
						}

						res = (b1 ^ b1).reveal(PUBLIC);
						if (res) {
							cout<<"XOR" <<i<<" "<<j<<res<<endl;
							error("test bit error!");
						}

						res = (b1 ^ (!b1)).reveal(PUBLIC);
						if (!res) {
							cout<<"XOR" <<i<<" "<<j<<res<<endl;
							error("test bit error!");
						}

					}
				}
	cout <<"success!"<<endl;
}

int main(int argc, char** argv) {
	setup_plain_prot(false, "");
	test_bit<PlainCircExec>();
	finalize_plain_prot();
}
