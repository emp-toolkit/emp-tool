#ifndef EMP_BRISTOL_FORMAT_H__
#define EMP_BRISTOL_FORMAT_H__
#include "emp-tool/core/constants.h"
#include "emp-tool/execution/backend.h"
#include "emp-tool/circuits/bit.h"
#include <stdio.h>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
using std::vector;
namespace emp {

// Bristol parsing — defensive validation.
//
// `.txt` circuit files are an interchange format and are routinely
// loaded from third-party sources (Bristol's standard library, MPC
// benchmark suites, generated/printed circuits). Trusting the header
// counts or gate indices verbatim opens a CWE-121 stack smash (via
// %s into a fixed buffer) and CWE-787 OOB write (via wire indices
// that exceed num_wire). The parser below validates every field at
// read time and throws std::runtime_error on malformed input.
//
// Validation policy:
//   - num_gate, num_wire, fan-in/out counts must be >= 0.
//   - %s scans are width-limited to the destination buffer minus 1.
//   - fscanf return values are checked; short reads throw.
//   - Per-gate arity ∈ {1, 2}; the type token must start with the
//     letter matching the arity (A/X for 2, anything for arity-1 →
//     NOT). Unknown values throw rather than leaving slots zero.
//   - Every wire index emitted into `gates[]` satisfies idx < num_wire.

inline void validate_wire_idx(int idx, int num_wire, const char *ctx) {
	if (idx < 0 || idx >= num_wire)
		throw std::runtime_error(std::string(ctx) + ": wire index out of range");
}

template<typename T, typename Wire>
void execute_circuit(Bit_T<Wire> * wires, const T * gates, size_t num_gate) {
	for(size_t i = 0; i < num_gate; ++i) {
		if(gates[4*i+3] == AND_GATE) {
			wires[gates[4*i+2]] = wires[gates[4*i]] & wires[gates[4*i+1]];
		}
		else if (gates[4*i+3] == XOR_GATE) {
			wires[gates[4*i+2]] = wires[gates[4*i]] ^ wires[gates[4*i+1]];
		}
		else if (gates[4*i+3] == NOT_GATE) {
			wires[gates[4*i+2]] = !wires[gates[4*i]];
		} else {
			assert(false);
		}
	}
}

class BristolFormat { public:
	int num_gate, num_wire, n1, n2, n3;
	vector<int> gates;
	std::ofstream fout;

	BristolFormat(int num_gate, int num_wire, int n1, int n2, int n3, int * gate_arr) {
		this->num_gate = num_gate;
		this->num_wire = num_wire;
		this->n1 = n1;
		this->n2 = n2;
		this->n3 = n3;
		gates.resize(num_gate*4);
		memcpy(gates.data(), gate_arr, num_gate*4*sizeof(int));
	}

	BristolFormat(FILE * file) {
		this->from_file(file);
	}

	BristolFormat(const char * file) {
		this->from_file(file);
	}

	void to_file(const char * filename, const char * prefix) {
		fout.open(filename);
		fout << "int "<<std::string(prefix)+"_num_gate = "<<num_gate<<";\n";
		fout << "int "<<std::string(prefix)+"_num_wire = "<<num_wire<<";\n";
		fout << "int "<<std::string(prefix)+"_n1 = "<<n1<<";\n";
		fout << "int "<<std::string(prefix)+"_n2 = "<<n2<<";\n";
		fout << "int "<<std::string(prefix)+"_n3 = "<<n3<<";\n";
		fout << "int "<<std::string(prefix)+"_gate_arr ["<< num_gate*4 <<"] = {\n";
		for(int i = 0; i < num_gate; ++i) {
			for(int j = 0; j < 4; ++j)
				fout<<gates[4*i+j]<<", ";
			fout<<"\n";
		}
		fout <<"};\n";
		fout.close();
	}


	void from_file(FILE * f) {
		if (fscanf(f, "%d%d\n", &num_gate, &num_wire) != 2)
			throw std::runtime_error("BristolFormat: malformed header (num_gate num_wire)");
		if (num_gate < 0 || num_wire < 0)
			throw std::runtime_error("BristolFormat: negative num_gate or num_wire");
		if (fscanf(f, "%d%d%d\n", &n1, &n2, &n3) != 3)
			throw std::runtime_error("BristolFormat: malformed header (n1 n2 n3)");
		if (n1 < 0 || n2 < 0 || n3 < 0)
			throw std::runtime_error("BristolFormat: negative fan-in/out");
		if ((long long)n1 + (long long)n2 > (long long)num_wire ||
		    (long long)n3 > (long long)num_wire)
			throw std::runtime_error("BristolFormat: fan-in/out exceeds num_wire");
		(void)fscanf(f, "\n");
		char str[200];
		gates.resize((size_t)num_gate * 4);
		for(int i = 0; i < num_gate; ++i) {
			int arity = 0;
			if (fscanf(f, "%d", &arity) != 1)
				throw std::runtime_error("BristolFormat: missing gate arity");
			if (arity == 2) {
				int fanout = 0; (void)fanout;
				if (fscanf(f, "%d%d%d%d%199s",
				           &fanout, &gates[4*i], &gates[4*i+1], &gates[4*i+2], str) != 5)
					throw std::runtime_error("BristolFormat: malformed 2-input gate");
				validate_wire_idx(gates[4*i],   num_wire, "BristolFormat");
				validate_wire_idx(gates[4*i+1], num_wire, "BristolFormat");
				validate_wire_idx(gates[4*i+2], num_wire, "BristolFormat");
				if (str[0] == 'A')      gates[4*i+3] = AND_GATE;
				else if (str[0] == 'X') gates[4*i+3] = XOR_GATE;
				else throw std::runtime_error(std::string("BristolFormat: unknown 2-input gate type '") + str + "'");
			}
			else if (arity == 1) {
				int fanout = 0; (void)fanout;
				if (fscanf(f, "%d%d%d%199s",
				           &fanout, &gates[4*i], &gates[4*i+2], str) != 4)
					throw std::runtime_error("BristolFormat: malformed 1-input gate");
				validate_wire_idx(gates[4*i],   num_wire, "BristolFormat");
				validate_wire_idx(gates[4*i+2], num_wire, "BristolFormat");
				gates[4*i+1] = 0;
				gates[4*i+3] = NOT_GATE;
			}
			else {
				throw std::runtime_error("BristolFormat: unsupported gate arity");
			}
		}
	}

	void from_file(const char * file) {
		FILE * f = fopen(file, "r");
		if (!f) throw std::runtime_error(std::string("BristolFormat: cannot open ") + file);
		try { this->from_file(f); } catch (...) { fclose(f); throw; }
		fclose(f);
	}

	template<typename Wire>
	void compute(Bit_T<Wire> * out, const Bit_T<Wire>* in1, const Bit_T<Wire> * in2) {
		vector<Bit_T<Wire>> wires(num_wire);
		for(int i = 0; i < n1; ++i)
			wires[i] = in1[i];
		for(int i = 0; i < n2; ++i)
			wires[i+n1] = in2[i];
		execute_circuit(wires.data(), gates.data(), num_gate);
		for(int i = 0; i < n3; ++ i)
			out[i] = wires[num_wire-n3+i];
	}
};

class BristolFashion { public:
	int num_gate = 0, num_wire = 0, 
		 num_input = 0, num_output = 0;
	vector<int> gates;

	BristolFashion(FILE * file) {
		this->from_file(file);
	}

	BristolFashion(const char * file) {
		this->from_file(file);
	}

	void from_file(FILE * f) {
		if (fscanf(f, "%d%d\n", &num_gate, &num_wire) != 2)
			throw std::runtime_error("BristolFashion: malformed header (num_gate num_wire)");
		if (num_gate < 0 || num_wire < 0)
			throw std::runtime_error("BristolFashion: negative num_gate or num_wire");
		int niov = 0;
		int tmp = 0;
		if (fscanf(f, "%d", &niov) != 1 || niov < 0)
			throw std::runtime_error("BristolFashion: malformed input-vector count");
		for(int i = 0; i < niov; ++i) {
			if (fscanf(f, "%d", &tmp) != 1 || tmp < 0)
				throw std::runtime_error("BristolFashion: malformed input-vector width");
			if ((long long)num_input + (long long)tmp > (long long)num_wire)
				throw std::runtime_error("BristolFashion: input widths exceed num_wire");
			num_input += tmp;
		}
		if (fscanf(f, "%d", &niov) != 1 || niov < 0)
			throw std::runtime_error("BristolFashion: malformed output-vector count");
		for(int i = 0; i < niov; ++i) {
			if (fscanf(f, "%d", &tmp) != 1 || tmp < 0)
				throw std::runtime_error("BristolFashion: malformed output-vector width");
			if ((long long)num_output + (long long)tmp > (long long)num_wire)
				throw std::runtime_error("BristolFashion: output widths exceed num_wire");
			num_output += tmp;
		}

		char str[10];
		gates.resize((size_t)num_gate * 4);
		for(int i = 0; i < num_gate; ++i) {
			int arity = 0;
			if (fscanf(f, "%d", &arity) != 1)
				throw std::runtime_error("BristolFashion: missing gate arity");
			if (arity == 2) {
				int fanout = 0; (void)fanout;
				if (fscanf(f, "%d%d%d%d%9s",
				           &fanout, &gates[4*i], &gates[4*i+1], &gates[4*i+2], str) != 5)
					throw std::runtime_error("BristolFashion: malformed 2-input gate");
				validate_wire_idx(gates[4*i],   num_wire, "BristolFashion");
				validate_wire_idx(gates[4*i+1], num_wire, "BristolFashion");
				validate_wire_idx(gates[4*i+2], num_wire, "BristolFashion");
				if (str[0] == 'A')      gates[4*i+3] = AND_GATE;
				else if (str[0] == 'X') gates[4*i+3] = XOR_GATE;
				else throw std::runtime_error(std::string("BristolFashion: unknown 2-input gate type '") + str + "'");
			}
			else if (arity == 1) {
				int fanout = 0; (void)fanout;
				if (fscanf(f, "%d%d%d%9s",
				           &fanout, &gates[4*i], &gates[4*i+2], str) != 4)
					throw std::runtime_error("BristolFashion: malformed 1-input gate");
				validate_wire_idx(gates[4*i],   num_wire, "BristolFashion");
				validate_wire_idx(gates[4*i+2], num_wire, "BristolFashion");
				gates[4*i+1] = 0;
				gates[4*i+3] = NOT_GATE;
			}
			else {
				throw std::runtime_error("BristolFashion: unsupported gate arity");
			}
		}
	}

	void from_file(const char * file) {
		FILE * f = fopen(file, "r");
		if (!f) throw std::runtime_error(std::string("BristolFashion: cannot open ") + file);
		try { this->from_file(f); } catch (...) { fclose(f); throw; }
		fclose(f);
	}

	template<typename Wire>
	void compute(Bit_T<Wire> * out, const Bit_T<Wire> * in) {
		vector<Bit_T<Wire>> wires(num_wire);
		for(int i = 0; i < num_input; ++i)
			wires[i] = in[i];
		execute_circuit(wires.data(), gates.data(), num_gate);

		for(int i = 0; i < num_output; ++i)
			out[i] = wires[num_wire - num_output + i];
	}
};

}
#endif// CIRCUIT_FILE
