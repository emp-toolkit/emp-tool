#ifndef UTILS_EC_H__
#define UTILS_EC_H__
extern "C" {
#include <relic/relic.h>
}
#include "block.h"

#include "hash.h"
#define ECC_PACK false
#define BIT_LEN 128
#define EB_SIZE 65


typedef eb_t eb_tpl[2];
typedef bn_t bn_tpl[2];

#define __batch(func, ...)\
	template <typename H, typename... T> void func##l(H p, T... t) {\
		func(p,##__VA_ARGS__);\
		func##l(t...);\
	}\
	template <typename H> void func##l(H p) {\
		func(p,##__VA_ARGS__);\
	}

__batch(eb_new);
__batch(eb_free);
__batch(bn_new);
__batch(bn_free);

#define __batch2(func)\
template <typename H1, typename H2, typename H3> void func##_norm(H1 h1, H2 h2, H3 h3) {\
	func(h1, h2, h3);\
	eb_norm(h1, h1);\
}\

__batch2(eb_mul);
__batch2(eb_mul_fix);
__batch2(eb_sub);
__batch2(eb_add);

#define __batch3(func)\
template <typename H1, typename H2, typename H3, typename H4> void func##_mod(H1 h1, H2 h2, H3 h3, H4 h4) {\
	func(h1, h2, h3);\
	bn_mod(h1, h1, h4);\
}\

__batch3(bn_mul);
__batch3(bn_sub);
__batch3(bn_add);

void bn_to_block(block * b, const bn_t bn);
void block_to_bn(bn_t bn, const block * b);
static bool initialized = false;
void initialize_relic();

block KDF(eb_t in);
#include "utils_ec.hpp"
#endif// UTILS_EC_H__
