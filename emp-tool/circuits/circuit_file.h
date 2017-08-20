#ifndef CIRCUIT_FILE
#define CIRCUIT_FILE

#include "emp-tool/execution/circuit_execution.h"
#include "emp-tool/execution/protocol_execution.h"
#include "emp-tool/utils/block.h"
#include "emp-tool/circuits/bit.h"
#include <stdio.h>

namespace emp {
#define AND_GATE 0
#define XOR_GATE 1
#define NOT_GATE 2

class CircuitFile { 
public:
	int num_gate, num_wire, n1, n2, n3;
	int *gates;
	block * wires;
	int tmp, tmp2;
	CircuitFile(const char * file) {
		FILE * f = fopen(file, "r");
		tmp2=fscanf(f, "%d%d\n", &num_gate, &num_wire);
		tmp2=fscanf(f, "%d%d%d\n", &n1, &n2, &n3);
		tmp2=fscanf(f, "\n");
		char str[10];
		gates = new int[num_gate*4];
		wires = new block[num_wire];
		for(int i = 0; i < num_gate; ++i) {
			tmp2=fscanf(f, "%d", &tmp);
			if (tmp == 2) {
				tmp2=fscanf(f, "%d%d%d%d%s", &tmp, &gates[4*i], &gates[4*i+1], &gates[4*i+2], str);
				if (str[0] == 'A') gates[4*i+3] = AND_GATE;
				else if (str[0] == 'X') gates[4*i+3] = XOR_GATE;
			}
			else if (tmp == 1) {
				tmp2=fscanf(f, "%d%d%d%s", &tmp, &gates[4*i], &gates[4*i+2], str);
				gates[4*i+3] = NOT_GATE;
			}
		}
		fclose(f);
	}

	CircuitFile(const CircuitFile& cf) {
		num_gate = cf.num_gate;
		num_wire = cf.num_wire;
		n1 = cf.n1;
		n2 = cf.n2;
		n3 = cf.n3;
		gates = new int[num_gate*4];
		wires = new block[num_wire];
		memcpy(gates, cf.gates, num_gate*4*sizeof(int));
		memcpy(wires, cf.wires, num_wire*sizeof(block));	
	}
	~CircuitFile(){
		delete[] gates;
		delete[] wires;
	}
	int table_size() const{
		return num_gate*4;
	}

	void compute(block * out, block * in1, block * in2) {
		memcpy(wires, in1, n1*sizeof(block));
		memcpy(wires+n1, in2, n2*sizeof(block));
		for(int i = 0; i < num_gate; ++i) {
			if(gates[4*i+3] == AND_GATE) {
				wires[gates[4*i+2]] = CircuitExecution::circ_exec->and_gate(wires[gates[4*i]], wires[gates[4*i+1]]);
			}
			else if (gates[4*i+3] == XOR_GATE) {
				wires[gates[4*i+2]] = CircuitExecution::circ_exec->xor_gate(wires[gates[4*i]], wires[gates[4*i+1]]);
			}
			else  
				wires[gates[4*i+2]] = CircuitExecution::circ_exec->not_gate(wires[gates[4*i]]);
		}
		memcpy(out, &wires[num_wire-n3], n3*sizeof(block));
	}
};
}
#endif// CIRCUIT_FILE
