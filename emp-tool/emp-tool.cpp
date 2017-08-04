#include "emp-tool/execution/circuit_execution.h"
#include "emp-tool/execution/protocol_execution.h"
#include "emp-tool/utils/prg.h"

PRG* PRG::rnd = nullptr;
ProtocolExecution* ProtocolExecution::prot_exec = nullptr;
CircuitExecution* CircuitExecution::circ_exec = nullptr;
