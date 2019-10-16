#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;
using namespace emp;

void aes_k2_h2_ori(block *H, block random, uint64_t idx) {
	block user_key[2];
	user_key[0] = xorBlocks(makeBlock(2 * idx, (uint64_t)0), random);
	user_key[1] = xorBlocks(makeBlock(2 * idx + 1, (uint64_t)0), random);

	AES_KEY keys[2];
	AES_set_encrypt_key(user_key[0], keys[0]);
	AES_set_encrypt_key(user_key[1], keys[1]);

	AES_ecb_encrypt_blks(&H[0], 1, &keys[0]);
	AES_ecb_encrypt_blks(&H[1], 1, &keys[1]);
}

void aes_k2_h4_ori(block *H, block random, uint64_t idx) {
	block user_key[2];
	user_key[0] = xorBlocks(makeBlock(2 * idx, (uint64_t)0), random);
	user_key[1] = xorBlocks(makeBlock(2 * idx + 1, (uint64_t)0), random);

	AES_KEY keys[2];
	AES_set_encrypt_key(user_key[0], keys[0]);
	AES_set_encrypt_key(user_key[1], keys[1]);

	AES_ecb_encrypt_blks(&H[0], 1, &keys[0]);
	AES_ecb_encrypt_blks(&H[1], 1, &keys[0]);
	AES_ecb_encrypt_blks(&H[2], 1, &keys[1]);
	AES_ecb_encrypt_blks(&H[3], 1, &keys[1]);
}

void aes_k8_h8_ori(block *H, block random, uint64_t idx) {
	block user_key[8];
	user_key[0] = xorBlocks(makeBlock(2 * idx, (uint64_t)0), random);
	user_key[1] = xorBlocks(makeBlock(2 * idx + 1, (uint64_t)0), random);
	idx++;
	user_key[2] = xorBlocks(makeBlock(2 * idx, (uint64_t)0), random);
	user_key[3] = xorBlocks(makeBlock(2 * idx + 1, (uint64_t)0), random);
	idx++;
	user_key[4] = xorBlocks(makeBlock(2 * idx, (uint64_t)0), random);
	user_key[5] = xorBlocks(makeBlock(2 * idx + 1, (uint64_t)0), random);
	idx++;
	user_key[6] = xorBlocks(makeBlock(2 * idx, (uint64_t)0), random);
	user_key[7] = xorBlocks(makeBlock(2 * idx + 1, (uint64_t)0), random);

	AES_KEY keys[8];
	for(int i = 0; i < 8; i++)
		AES_set_encrypt_key(user_key[i], keys[i]);

	for(int i = 0; i < 8; i++)
		AES_ecb_encrypt_blks(&H[i], 1, &keys[i]);
}

int main() {
	PRG prg;//using a random seed
	MITCCRH mitccrh;
	block start_point;
	prg.random_block(&start_point, 1);
	mitccrh.setS(start_point);

	// 2 key schedules for 2 hashes
	uint64_t gid = 0;
	while(gid < 1024) {
		if(mitccrh.key_used == KS_BATCH_N)
			mitccrh.renew_ks(gid);
		block hash[2];
		prg.random_block(&hash, 2);
		block out[2];
		mitccrh.k2_h2(hash[0], hash[1], out);
		aes_k2_h2_ori(hash, start_point, gid);

		if(cmpBlock(hash, out, 2) == false)
			cout << "incorrect encryption for k2 h2" << endl;
		gid++;
	}

	// 2 key schedules for 4 hashes
	gid = 0;
	while(gid < 1024) {
		if(mitccrh.key_used == KS_BATCH_N)
			mitccrh.renew_ks(gid);
		block hash[4];
		prg.random_block(&hash, 4);
		block out[4];
		mitccrh.k2_h4(hash[0], hash[1], hash[2], hash[3], out);
		aes_k2_h4_ori(hash, start_point, gid);

		if(cmpBlock(hash, out, 4) == false)
			cout << "incorrect encryption for k2 h4" << endl;
		gid++;
	}


	// 8 key schedules for 8 hashes (out of mitrrch)
	gid = 0;
	while(gid < 1024) {
		ROUND_KEYS keys[8];
		block random;
		prg.random_block(&random, 1);
		AES_ks8_circ(random, gid, keys);
		block plaintext[8], ciphertext[8];
		prg.random_block(plaintext, 8);
		AES_ecb_ccr_ks8_enc8(plaintext, ciphertext, keys);

		aes_k8_h8_ori(plaintext, random, gid)
		if(cmpBlock(plaintext, ciphertext, 8) == false)
			cout << "incorrect encryption for k8 h8" << endl;
		gid++;
	}

	return 0;
}
