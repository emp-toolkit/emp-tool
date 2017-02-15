#include "circuit_generator.h"

void test_bit() {
	bool b[] = {true, false};
	int p[] = {PUBLIC, ALICE, BOB};

	for(int i = 0; i < 2; ++i)
		for(int j = 0; j < 3; ++j)
			for(int k = 0; k < 2; ++k)
				for (int l= 0; l < 3; ++l)  {
					{
						Bit b1(b[i], p[j]);
						Bit b2(b[k], p[l]);
						bool res = (b1&b2).reveal(BOB);
						if(res != (b[i] and b[k]))
							cout<<"AND" <<i<<" "<<j<<" "<<k<<" "<<l<<" "<<res<<endl;
						assert(res == (b[i] and b[k]));
					}
					{
						Bit b1(b[i], p[j]);
						Bit b2(b[k], p[l]);
						bool res = (b1^b2).reveal(BOB);
						if(res != (b[i] xor b[k]))
							cout <<"XOR"<<i<<" "<<j<<" "<<k<<" "<<l<< " " <<res<<endl;
						assert(res == (b[i] xor b[k]));
					}
				}
	cout <<"success!"<<endl;
}

int main(int argc, char** argv) {
	setup_circuit_generator(true, "mul");
	test_bit();
}
