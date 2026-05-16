#include "emp-tool/core/constants.h"
#include "emp-tool/execution/backend.h"

namespace emp {

#ifndef THREADING
Backend* backend = nullptr;
#else
__thread Backend* backend = nullptr;
#endif

}  // namespace emp
