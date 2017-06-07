#include <thread>
#include "backend.h"

#ifdef THREADING
thread_local Backend* local_backend = nullptr;
thread_local GarbleCircuit* local_gc = nullptr;
#else
Backend* local_backend = nullptr;
GarbleCircuit* local_gc = nullptr;
#endif

