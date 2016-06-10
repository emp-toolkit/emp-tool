#include "backend.h"

#ifdef THREADING
__thread Backend* local_backend = nullptr;
__thread GarbleCircuit* local_gc = nullptr;
#else
Backend* local_backend = nullptr;
GarbleCircuit* local_gc = nullptr;
#endif

