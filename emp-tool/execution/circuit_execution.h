#ifndef CIRCUIT_EXECUTION_H__
#define CIRCUIT_EXECUTION_H__
#include "emp-tool/utils/block.h"
namespace emp {
class CircuitExecution { 
public:
	static CircuitExecution * circ_exec;
	virtual block and_gate(const block& in1, const block& in2) = 0;
	virtual block xor_gate(const block&in1, const block&in2) = 0;
	virtual block not_gate(const block& in1) = 0;
	virtual block public_label(bool b) = 0;
	virtual ~CircuitExecution (){ }
};
enum RTCktOpt{on, off};
}
#endif