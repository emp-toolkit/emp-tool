// Link-time build-consistency guards (runtime/core/abi_guard.h). Without
// ABI_NEG_CASE this is an ordinary consumer and must link. Each negative case
// flips one build option for this translation unit only, so its guard points
// at a marker symbol libemp-tool does not define and the link must fail.
#if ABI_NEG_CASE == 1  // the other test mode
#ifdef EMP_TEST_MODE
#undef EMP_TEST_MODE
#else
#define EMP_TEST_MODE
#endif
#elif ABI_NEG_CASE == 2  // the other default Hash backend
#undef EMP_HASH_DEFAULT
#define EMP_HASH_DEFAULT ABI_OTHER_HASH
#elif ABI_NEG_CASE == 3  // the Fiat-Shamir state without BLAKE3
#undef EMP_WITH_BLAKE3
#undef EMP_HASH_DEFAULT
#undef EMP_FS_HASH_DEFAULT
#endif

#include "emp-tool/runtime/core/test_mode.h"
#include "emp-tool/runtime/crypto/hash.h"

int main() { return 0; }
