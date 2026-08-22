#include "emp-tool/runtime/crypto/aes.h"

using namespace emp;

int main() {
	block blocks[4] = {};
	AES_KEY keys[1] = {};
#if AES_NEG_CASE == 1
	ParaEnc<-1, 4>(blocks, keys);
#elif AES_NEG_CASE == 2
	ParaEnc<1, -4>(blocks, keys);
#elif AES_NEG_CASE == 3
	AES_opt_key_schedule<-1>(blocks, keys);
#elif AES_NEG_CASE == 4
	aes_ctr_fill_dm<-1>(blocks, 0, keys);
#endif
	return 0;
}
