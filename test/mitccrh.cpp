#include "emp-tool/emp-tool.h"
#include <iostream>
using namespace std;
using namespace emp;

void print_round_keys(ROUND_KEYS key) {
	printf("round keys\n");
	for(int i = 0; i < 11; i++) {
		for(int j = 0; j < 16; j++)
			printf("%02X", key.KEY[i*16+j]);
		printf("\n"); 
	}
}

void print_aes_key(AES_KEY key) {
	printf("aes keys\n");
	for(int i = 0; i < 11; i++) {
		unsigned char *p = (unsigned char*)(&key.rd_key[i]);
		for(int j = 0; j < 16; j++)
			printf("%02X", p[j]);
		printf("\n");
	}
}

void print_block(block a) {
	printf("plaintext or ciphertext\n");
	unsigned char *p = (unsigned char*)(&a);
	for(int j = 0; j < 16; j++)
		printf("%02X", p[j]);
	printf("\n");
}

void aes_k2_h2_ori(block *H, block random, uint64_t idx) {
	block user_key[2];
	user_key[0] = xorBlocks(makeBlock(2 * idx, (uint64_t)0), random);
	user_key[1] = xorBlocks(makeBlock(2 * idx + 1, (uint64_t)0), random);

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

	H[0] = xorBlocks(keys1[0], masks[0]);
	H[1] = xorBlocks(keys1[1], masks[1]);
}

void aes_k2_h4_ori(block *H, block random, uint64_t idx) {
	block user_key[2];
	user_key[0] = xorBlocks(makeBlock(2 * idx, (uint64_t)0), random);
	user_key[1] = xorBlocks(makeBlock(2 * idx + 1, (uint64_t)0), random);

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

	H[0] = xorBlocks(keys1[0], masks[0]);
	H[1] = xorBlocks(keys1[1], masks[1]);
	H[2] = xorBlocks(keys1[2], masks[2]);
	H[3] = xorBlocks(keys1[3], masks[3]);
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
		AES_set_encrypt_key(user_key[i], &keys[i]);

	for(int i = 0; i < 8; i++)
		AES_ecb_encrypt_blks(&H[i], 1, &keys[i]);
}

void test_encryption() {
	PRG prg;
	block ptx[2], key[2];
	prg.random_block(ptx, 2);
	prg.random_block(key, 2);
	
	ROUND_KEYS keys1[2];
	AES_ks2(key, keys1);
	block ctx[2];
	AES_ecb_ccr_ks2_enc2(ptx, ctx, keys1);

	print_block(ctx[0]);
	print_block(ctx[1]);

	AES_KEY keys2[2];
	AES_set_encrypt_key(key[0], &keys2[0]);
	AES_set_encrypt_key(key[1], &keys2[1]);
	AES_ecb_encrypt_blks(&ptx[0], 1, &keys2[0]);
	AES_ecb_encrypt_blks(&ptx[1], 1, &keys2[1]);
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
		prg.random_block(hash, 2);
		block out[2];
		mitccrh.k2_h2(hash[0], hash[1], out);
		aes_k2_h2_ori(hash, start_point, gid);

		if(cmpBlock(hash, out, 2) == false) {
			cout << "incorrect encryption for k2 h2" << endl;
			exit(1);
		}
		gid++;
	}

	// 2 key schedules for 4 hashes
	gid = 0;
	MITCCRH mitccrh1;
	mitccrh1.setS(start_point);
	while(gid < 1024) {
		if(mitccrh1.key_used == KS_BATCH_N)
			mitccrh1.renew_ks(gid);
		block hash[4];
		prg.random_block(hash, 4);
		block out[4];
		mitccrh1.k2_h4(hash[0], hash[1], hash[2], hash[3], out);
		aes_k2_h4_ori(hash, start_point, gid);

		if(cmpBlock(hash, out, 4) == false) {
			cout << "incorrect encryption for k2 h4" << endl;
			exit(1);
		}
		gid++;
	}


	// 8 key schedules for 8 hashes (out of mitrrch)
	gid = 0;
	while(gid < 1024) {
		ROUND_KEYS keys[8];
		block random;
		prg.random_block(&random, 1);
		AES_ks8_index(random, gid, keys);
		block plaintext[8], ciphertext[8];
		prg.random_block(plaintext, 8);
		AES_ecb_ccr_ks8_enc8(plaintext, ciphertext, keys);

		aes_k8_h8_ori(plaintext, random, gid);
		if(cmpBlock(plaintext, ciphertext, 8) == false) {
			cout << "incorrect encryption for k8 h8" << endl; 
			exit(1);
		}
		gid++;
	}

	cout << "PASS"<<endl;

	return 0;
}
