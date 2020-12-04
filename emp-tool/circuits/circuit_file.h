#ifndef EMP_BRISTOL_FORMAT_H__
#define EMP_BRISTOL_FORMAT_H__

#include "emp-tool/execution/circuit_execution.h"
#include "emp-tool/execution/protocol_execution.h"
#include "emp-tool/utils/block.h"
#include "emp-tool/circuits/bit.h"
#include <stdio.h>
using std::vector;

namespace emp {
#define AND_GATE 0
#define XOR_GATE 1
#define NOT_GATE 2

class BristolFormat { public:
	int num_gate, num_wire, n1, n2, n3;
	vector<int> gates;
	vector<block> wires;
	BristolFormat(const char * file) {
		int tmp;
		FILE * f = fopen(file, "r");
		(void)fscanf(f, "%d%d\n", &num_gate, &num_wire);
		(void)fscanf(f, "%d%d%d\n", &n1, &n2, &n3);
		(void)fscanf(f, "\n");
		char str[10];
		gates.resize(num_gate*4);
		wires.resize(num_wire);
		for(int i = 0; i < num_gate; ++i) {
			(void)fscanf(f, "%d", &tmp);
			if (tmp == 2) {
				(void)fscanf(f, "%d%d%d%d%s", &tmp, &gates[4*i], &gates[4*i+1], &gates[4*i+2], str);
				if (str[0] == 'A') gates[4*i+3] = AND_GATE;
				else if (str[0] == 'X') gates[4*i+3] = XOR_GATE;
			}
			else if (tmp == 1) {
				(void)fscanf(f, "%d%d%d%s", &tmp, &gates[4*i], &gates[4*i+2], str);
				gates[4*i+3] = NOT_GATE;
			}
		}
		fclose(f);
	}
	void compute(Bit * out, const Bit * in1, const Bit * in2) {
		compute((block*)out, (block *)in1, (block*)in2);
	}

	void compute(block * out, const block * in1, const block * in2) {
		memcpy(wires.data(), in1, n1*sizeof(block));
		memcpy(wires.data()+n1, in2, n2*sizeof(block));
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
		memcpy(out, wires.data()+(num_wire-n3), n3*sizeof(block));
	}
};

class BristolFashion { public:
	int num_gate = 0, num_wire = 0, 
		 num_input = 0, num_output = 0;
	vector<int> gates;
	vector<block> wires;
	BristolFashion(const char * file) {
		int tmp;
		FILE * f = fopen(file, "r");
		(void)fscanf(f, "%d%d\n", &num_gate, &num_wire);
		int niov = 0;
		(void)fscanf(f, "%d", &niov);
		for(int i = 0; i < niov; ++i) {
			(void)fscanf(f, "%d", &tmp);
			num_input += tmp;
		}
		(void)fscanf(f, "%d", &niov);
		for(int i = 0; i < niov; ++i) {
			(void)fscanf(f, "%d", &tmp);
			num_output += tmp;
		}

		char str[10];
		gates.resize(num_gate*4);
		wires.resize(num_wire);
		for(int i = 0; i < num_gate; ++i) {
			(void)fscanf(f, "%d", &tmp);
			if (tmp == 2) {
				(void)fscanf(f, "%d%d%d%d%s", &tmp, &gates[4*i], &gates[4*i+1], &gates[4*i+2], str);
				if (str[0] == 'A') gates[4*i+3] = AND_GATE;
				else if (str[0] == 'X') gates[4*i+3] = XOR_GATE;
			}
			else if (tmp == 1) {
				(void)fscanf(f, "%d%d%d%s", &tmp, &gates[4*i], &gates[4*i+2], str);
				gates[4*i+3] = NOT_GATE;
			}
		}
		fclose(f);
	}

	void compute(Bit * out, const Bit * in) {
		compute((block*)out, (block *)in);
	}
	void compute(block * out, const block * in) {
		memcpy(wires.data(), in, num_input*sizeof(block));
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
		memcpy(out, wires.data()+(num_wire-num_output), num_output*sizeof(block));
	}
};

}
#endif// CIRCUIT_FILE
