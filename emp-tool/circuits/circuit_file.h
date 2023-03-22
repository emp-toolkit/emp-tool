#ifndef EMP_BRISTOL_FORMAT_H__
#define EMP_BRISTOL_FORMAT_H__
#include "emp-tool/execution/backend.h"
#include "emp-tool/circuits/bit.h"
#include <stdio.h>
#include <fstream>
#include <vector>
using std::vector;
namespace emp {

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


	void from_file(FILE * f) {
		int tmp;
		(void)fscanf(f, "%d%d\n", &num_gate, &num_wire);
		(void)fscanf(f, "%d%d%d\n", &n1, &n2, &n3);
		(void)fscanf(f, "\n");
		char str[200];
		gates.resize(num_gate*4);
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
	}

	void from_file(const char * file) {
		FILE * f = fopen(file, "r");
		this->from_file(f);
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
		int tmp;
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
	}

	void from_file(const char * file) {
		FILE * f = fopen(file, "r");
		this->from_file(f);
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
