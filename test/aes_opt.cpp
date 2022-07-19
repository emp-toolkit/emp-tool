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
	
	block key = makeBlock(0x0f0e0d0c0b0a0908, 0x0706050403020100);
	block msg = makeBlock(0xffeeddccbbaa9988, 0x7766554433221100);
	block res = makeBlock(0x5ac5b47080b7cdd8, 0x30047b6ad8e0c469);//https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.197.pdf page 36
	AES_KEY KEY[2];
	AES_set_encrypt_key(key, KEY);
	AES_ecb_encrypt_blks(&msg, 1, KEY);
	if(!cmpBlock(&msg, &res, 1)) {
		error("AES test vector fail!");
	}
	
	block msg2[2];
	msg2[0] = msg2[1] = makeBlock(0xffeeddccbbaa9988, 0x7766554433221100);
	AES_set_encrypt_key(key, KEY);
	AES_set_encrypt_key(key, KEY+1);
	ParaEnc<2, 1>(msg2, KEY);
	if(!cmpBlock(msg2, &res, 1) or !cmpBlock(msg2+1, &res, 1)) {
		error("AES test vector fail!");
	}

	msg2[0] = msg2[1] = makeBlock(0xffeeddccbbaa9988, 0x7766554433221100);
	ParaEnc<1, 2>(msg2, KEY);
	if(!cmpBlock(msg2, &res, 1) or !cmpBlock(msg2+1, &res, 1)) {
		error("AES test vector fail!");
	}


	
	return 0;
}
