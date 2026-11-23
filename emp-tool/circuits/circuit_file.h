#ifndef EMP_BRISTOL_FORMAT_H__
#define EMP_BRISTOL_FORMAT_H__

#include "emp-tool/execution/circuit_execution.h"
#include "emp-tool/execution/protocol_execution.h"
#include "emp-tool/utils/block.h"
#include "emp-tool/circuits/bit.h"
#include <stdio.h>
#include <climits>
#include <cstring>
#include <fstream>

using std::vector;

namespace emp {
#define AND_GATE 0
#define XOR_GATE 1
#define NOT_GATE 2

template<typename T>
void execute_circuit(block * wires, const T * gates, size_t num_gate) {
	for(size_t i = 0; i < num_gate; ++i) {
		if(gates[4*i+3] == AND_GATE) {
			wires[gates[4*i+2]] = CircuitExecution::circ_exec->and_gate(wires[gates[4*i]], wires[gates[4*i+1]]);
		}
		else if (gates[4*i+3] == XOR_GATE) {
			wires[gates[4*i+2]] = CircuitExecution::circ_exec->xor_gate(wires[gates[4*i]], wires[gates[4*i+1]]);
		}
		else if (gates[4*i+3] == NOT_GATE) {
			wires[gates[4*i+2]] = CircuitExecution::circ_exec->not_gate(wires[gates[4*i]]);
		} else {
			block tmp = CircuitExecution::circ_exec->xor_gate(wires[gates[4*i]],  wires[gates[4*i+1]]);
			block tmp2 = CircuitExecution::circ_exec->and_gate(wires[gates[4*i]], wires[gates[4*i+1]]);
			wires[gates[4*i+2]] = CircuitExecution::circ_exec->xor_gate(tmp, tmp2);
		}
	}
}

// Circuit descriptions, circuit files in particular, are untrusted input.
// Every count and wire index is range-checked when the circuit is loaded,
// before compute() uses it to index `wires`; malformed input aborts through
// error(). Inputs occupy the first num_in wires and outputs the last num_out.
inline void check_bristol_counts(int num_gate, int num_wire, int64_t num_in, int64_t num_out) {
	if (num_gate < 0 || num_gate > INT_MAX / 4 || num_wire < 0
	    || num_in < 0 || num_in > num_wire || num_out < 0 || num_out > num_wire)
		error("circuit: invalid gate, wire, input or output count");
}

inline void check_bristol_gates(const int * gates, int num_gate, int num_wire) {
	for(int i = 0; i < num_gate; ++i) {
		const int * g = gates + 4*i;
		bool ok = g[0] >= 0 && g[0] < num_wire && g[2] >= 0 && g[2] < num_wire;
		if (g[3] == AND_GATE || g[3] == XOR_GATE)
			ok = ok && g[1] >= 0 && g[1] < num_wire;
		else if (g[3] != NOT_GATE)
			ok = false;
		if (!ok) error("circuit: invalid gate");
	}
}

// Reads one gate line into g[0..3]. Only the gates compute() evaluates are
// accepted, "2 1 in0 in1 out AND|XOR" and "1 1 in out INV"; any other line
// aborts rather than being skipped or misread.
inline void read_bristol_gate(FILE * f, int * g) {
	int nin = 0, nout = 0;
	char type[16];
	if (fscanf(f, "%d%d", &nin, &nout) != 2 || nout != 1)
		error("circuit file: malformed gate");
	if (nin == 2) {
		if (fscanf(f, "%d%d%d%15s", &g[0], &g[1], &g[2], type) != 4)
			error("circuit file: malformed gate");
		if (strcmp(type, "AND") == 0) g[3] = AND_GATE;
		else if (strcmp(type, "XOR") == 0) g[3] = XOR_GATE;
		else error("circuit file: unsupported gate type");
	} else if (nin == 1) {
		if (fscanf(f, "%d%d%15s", &g[0], &g[2], type) != 3)
			error("circuit file: malformed gate");
		if (strcmp(type, "INV") != 0)
			error("circuit file: unsupported gate type");
		g[1] = 0;
		g[3] = NOT_GATE;
	} else
		error("circuit file: unsupported gate arity");
}


class BristolFormat { public:
	int num_gate, num_wire, n1, n2, n3;
	vector<int> gates;
	vector<block> wires;
	std::ofstream fout;

	BristolFormat(int num_gate, int num_wire, int n1, int n2, int n3, int * gate_arr) {
		this->num_gate = num_gate;
		this->num_wire = num_wire;
		this->n1 = n1;
		this->n2 = n2;
		this->n3 = n3;
		check_counts();
		check_bristol_gates(gate_arr, num_gate, num_wire);
		gates.resize((size_t)num_gate*4);
		wires.resize(num_wire);
		memcpy(gates.data(), gate_arr, (size_t)num_gate*4*sizeof(int));
	}

	BristolFormat(FILE * file) {
		this->from_file(file);
	}

	BristolFormat(const char * file) {
		this->from_file(file);
	}


	void to_file(const char * filename, const char * prefix) {
		fout.open(filename);
		fout << "int "<<string(prefix)+"_num_gate = "<<num_gate<<";\n";
		fout << "int "<<string(prefix)+"_num_wire = "<<num_wire<<";\n";
		fout << "int "<<string(prefix)+"_n1 = "<<n1<<";\n";
		fout << "int "<<string(prefix)+"_n2 = "<<n2<<";\n";
		fout << "int "<<string(prefix)+"_n3 = "<<n3<<";\n";
		fout << "int "<<string(prefix)+"_gate_arr ["<< num_gate*4 <<"] = {\n";
		for(int i = 0; i < num_gate; ++i) {
			for(int j = 0; j < 4; ++j)
				fout<<gates[4*i+j]<<", ";
			fout<<"\n";
		}
		fout <<"};\n";
		fout.close();
	}


	void check_counts() const {
		if (n1 < 0 || n2 < 0)
			error("circuit: invalid gate, wire, input or output count");
		check_bristol_counts(num_gate, num_wire, (int64_t)n1 + n2, n3);
	}

	void from_file(FILE * f) {
		if (fscanf(f, "%d%d", &num_gate, &num_wire) != 2
		    || fscanf(f, "%d%d%d", &n1, &n2, &n3) != 3)
			error("circuit file: malformed header");
		check_counts();
		gates.resize((size_t)num_gate*4);
		wires.resize(num_wire);
		for(int i = 0; i < num_gate; ++i)
			read_bristol_gate(f, &gates[4*i]);
		check_bristol_gates(gates.data(), num_gate, num_wire);
	}

	void from_file(const char * file) {
		FILE * f = fopen(file, "r");
		if (f == nullptr) error("circuit file: cannot open");
		this->from_file(f);
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

	BristolFashion(FILE * file) {
		this->from_file(file);
	}

	BristolFashion(const char * file) {
		this->from_file(file);
	}

	// Sums the "count v_1 ... v_count" wire-count list of the header. Each
	// value is non-negative, so the int64_t sum cannot overflow.
	static int64_t read_io_wires(FILE * f) {
		int niov = 0, tmp = 0;
		int64_t total = 0;
		if (fscanf(f, "%d", &niov) != 1 || niov < 0)
			error("circuit file: malformed header");
		for(int i = 0; i < niov; ++i) {
			if (fscanf(f, "%d", &tmp) != 1 || tmp < 0)
				error("circuit file: malformed header");
			total += tmp;
		}
		return total;
	}

	void from_file(FILE * f) {
		if (fscanf(f, "%d%d", &num_gate, &num_wire) != 2)
			error("circuit file: malformed header");
		int64_t in_total = read_io_wires(f);
		int64_t out_total = read_io_wires(f);
		check_bristol_counts(num_gate, num_wire, in_total, out_total);
		num_input = (int)in_total;
		num_output = (int)out_total;

		gates.resize((size_t)num_gate*4);
		wires.resize(num_wire);
		for(int i = 0; i < num_gate; ++i)
			read_bristol_gate(f, &gates[4*i]);
		check_bristol_gates(gates.data(), num_gate, num_wire);
	}

	void from_file(const char * file) {
		FILE * f = fopen(file, "r");
		if (f == nullptr) error("circuit file: cannot open");
		this->from_file(f);
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
