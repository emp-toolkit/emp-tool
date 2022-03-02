#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;
using namespace emp;

int main() {
	for(int t = 0; t < 1000; ++t) {
		block key[8];
		PRG prg;prg.random_block(key, 8);
		AES_KEY key1[8], key2[8];
		AES_opt_key_schedule<8>(key, key1);
		for(int i = 0; i < 8; ++i) {
			AES_set_encrypt_key(key[i], key2+i);
			if(!cmpBlock(key1[i].rd_key, key2[i].rd_key, 11))
				error("AES test fail!");
		}
	}
	cout <<"all tests pass!\n";
	return 0;
}
