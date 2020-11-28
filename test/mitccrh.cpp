#include "emp-tool/emp-tool.h"
#include "emp-tool/utils/mitccrh.h"
#include <iostream>
using namespace std;
using namespace emp;

void print_keys(AES_KEY key) {
	cout << "round keys\n";
	for(int i = 0; i < 11; i++) {
		cout << key.rd_key[i]<<endl;
	}
}

void aes_k2_h2_ori(block *H, block random, uint64_t idx) {
	block user_key[2];
	user_key[0] = _mm_xor_si128(makeBlock(idx, (uint64_t)0), random);
	user_key[1] = _mm_xor_si128(makeBlock(idx + 1, (uint64_t)0), random);

	AES_KEY keys[2];
	AES_set_encrypt_key(user_key[0], &keys[0]);
	AES_set_encrypt_key(user_key[1], &keys[1]);

	block keys1[2], masks[2];
	keys1[0] = sigma(H[0]);
	keys1[1] = sigma(H[1]);
	masks[0] = keys1[0];
	masks[1] = keys1[1];

	AES_ecb_encrypt_blks(&keys1[0], 1, &keys[0]);
	AES_ecb_encrypt_blks(&keys1[1], 1, &keys[1]);

	H[0] = _mm_xor_si128(keys1[0], masks[0]);
	H[1] = _mm_xor_si128(keys1[1], masks[1]);
}

void aes_k2_h4_ori(block *H, block random, uint64_t idx) {
	block user_key[2];
	user_key[0] = _mm_xor_si128(makeBlock(idx, (uint64_t)0), random);
	user_key[1] = _mm_xor_si128(makeBlock(idx + 1, (uint64_t)0), random);

	AES_KEY keys[2];
	AES_set_encrypt_key(user_key[0], &keys[0]);
	AES_set_encrypt_key(user_key[1], &keys[1]);

	block keys1[4], masks[4];
	keys1[0] = sigma(H[0]);
	keys1[1] = sigma(H[1]);
	keys1[2] = sigma(H[2]);
	keys1[3] = sigma(H[3]);
	memcpy(masks, keys1, sizeof keys1);

	AES_ecb_encrypt_blks(&keys1[0], 1, &keys[0]);
	AES_ecb_encrypt_blks(&keys1[1], 1, &keys[0]);
	AES_ecb_encrypt_blks(&keys1[2], 1, &keys[1]);
	AES_ecb_encrypt_blks(&keys1[3], 1, &keys[1]);

	H[0] = _mm_xor_si128(keys1[0], masks[0]);
	H[1] = _mm_xor_si128(keys1[1], masks[1]);
	H[2] = _mm_xor_si128(keys1[2], masks[2]);
	H[3] = _mm_xor_si128(keys1[3], masks[3]);
}

int main() {
	PRG prg;//using a random seed
	MITCCRH<8> mitccrh;
	block start_point;
	prg.random_block(&start_point, 1);
	mitccrh.setS(start_point);

	// 2 key schedules for 2 hashes
	cout << "Correctness of 2 key scheduling and 2 blocks hasing ... ";
	uint64_t gid = 0;
	while(gid < 1024) {
		block hash[2];
		prg.random_block(hash, 2);
		block out[2];
		out[0] = hash[0];
		out[1] = hash[1];
		mitccrh.hash_cir<2,1>(out);
		aes_k2_h2_ori(hash, start_point, gid);

		if(cmpBlock(hash, out, 2) == false) {
			cout << "incorrect encryption for k2 h2" << endl;
			exit(1);
		}
		gid+=2;
	}
	cout << "correct" << endl;

	cout << "Benchmark: ";
	gid = 0;
	mitccrh.renew_ks(gid);
	block hash[8];
	prg.random_block(hash, 2);
	auto start = clock_start();
	while(gid < 1024*1024*10) {
		mitccrh.hash_cir<2,1>(hash);
		gid++;
	}
	cout << 1024*1024*10*2/(time_from(start))*1e6 << " blocks/second" << endl;

	// 2 key schedules for 4 hashes
	cout << "Correctness of 2 key scheduling and 4 blocks hasing ... ";
	gid = 0;
	MITCCRH<8> mitccrh1;
	mitccrh1.setS(start_point);
	while(gid < 1024) {
		block hash[4];
		prg.random_block(hash, 4);
		block out[4];
		out[0] = hash[0];
		out[1] = hash[1];
		out[2] = hash[2];
		out[3] = hash[3];
		mitccrh1.hash_cir<2,2>(out);
		aes_k2_h4_ori(hash, start_point, gid);

		if(cmpBlock(hash, out, 4) == false) {
			cout << "incorrect encryption for k2 h4" << endl;
			exit(1);
		}
		gid+=2;
	}
	cout << "correct" << endl;

	cout << "Benchmark: ";
	mitccrh1.renew_ks(0);
	prg.random_block(hash, 4);
	start = clock_start();
	while(mitccrh1.gid < 1024*1024*10) {
		mitccrh1.hash_cir<2,2>(hash);
	}
	cout << 1024*1024*10*4/(time_from(start))*1e6 << " blocks/second" << endl;

	
	cout << "Benchmark K8E8: ";
	gid = 0;
	prg.random_block(hash, 8);
	start = clock_start();
	mitccrh1.renew_ks(0);
	while(mitccrh1.gid < 1024*1024*10) {
		mitccrh1.hash_cir<8,1>(hash);
	}

	cout << 1024*1024*10*8/(time_from(start))*1e6 << " blocks/second" << endl;

	return 0;
}
