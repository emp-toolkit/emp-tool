#include "emp-tool/runtime/crypto/f2k.h"
#include "emp-tool/runtime/crypto/mitccrh.h"

using namespace emp;

int main() {
	block blocks[8] = {};
	bool bits[1] = {};
#if CRYPTO_NEG_CASE == 1
	MITCCRH<0> hash;
	(void)hash;
#elif CRYPTO_NEG_CASE == 2
	MITCCRH<8> hash;
	hash.hash<0, 1>(blocks);
#elif CRYPTO_NEG_CASE == 3
	MITCCRH<8> hash;
	hash.hash<1, 0>(blocks);
#elif CRYPTO_NEG_CASE == 4
	vector_inn_prdt_sum_no_red<-1>(blocks, blocks, blocks);
#elif CRYPTO_NEG_CASE == 5
	vector_inn_prdt_sum_red<-1>(blocks, blocks, blocks);
#elif CRYPTO_NEG_CASE == 6
	vector_inn_prdt_sum_red<-1>(blocks, blocks, bits);
#elif CRYPTO_NEG_CASE == 7
	vector_self_xor<-1>(blocks, blocks);
#elif CRYPTO_NEG_CASE == 8
	uni_hash_coeff_gen<0>(blocks, zero_block);
#elif CRYPTO_NEG_CASE == 9
	MITCCRH<-1> hash;
	(void)hash;
#elif CRYPTO_NEG_CASE == 10
	MITCCRH<8> hash;
	hash.hash<-1, 1>(blocks);
#elif CRYPTO_NEG_CASE == 11
	MITCCRH<8> hash;
	hash.hash<1, -1>(blocks);
#elif CRYPTO_NEG_CASE == 12
	uni_hash_coeff_gen<-1>(blocks, zero_block);
#endif
	return 0;
}
