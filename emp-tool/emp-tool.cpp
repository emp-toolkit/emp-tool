#include "emp-tool/core/constants.h"
#include "emp-tool/execution/backend.h"

namespace emp {

#ifndef THREADING
Backend* backend = nullptr;
#else
__thread Backend* backend = nullptr;
#endif

const char fix_key[17] = "\x61\x7e\x8d\xa2\xa0\x51\x1e\x96\x5e\x41\xc2\x9b\x15\x3f\xc7\x7a";

}  // namespace emp
