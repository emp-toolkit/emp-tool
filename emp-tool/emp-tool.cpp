#include "emp-tool/execution/circuit_execution.h"
#include "emp-tool/execution/protocol_execution.h"
#include "emp-tool/utils/constants.h"

#ifndef THREADING
emp::ProtocolExecution* emp::ProtocolExecution::prot_exec = nullptr;
emp::CircuitExecution* emp::CircuitExecution::circ_exec = nullptr;
#else
__thread emp::ProtocolExecution* emp::ProtocolExecution::prot_exec = nullptr;
__thread emp::CircuitExecution* emp::CircuitExecution::circ_exec = nullptr;
#endif

namespace emp {
const char fix_key[17] = "\x61\x7e\x8d\xa2\xa0\x51\x1e\x96\x5e\x41\xc2\x9b\x15\x3f\xc7\x7a";
}
