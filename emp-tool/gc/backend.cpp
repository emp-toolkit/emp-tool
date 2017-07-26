#include "emp-tool/gc/backend.h"
#include "emp-tool/utils/prg.h"

PRG * rnd = nullptr;

#ifdef THREADING
__thread Backend* local_backend = nullptr;
__thread GarbleCircuit* local_gc = nullptr;
#else
Backend* local_backend = nullptr;
GarbleCircuit* local_gc = nullptr;
#endif

