#ifndef CIRCUIT_EXECUTION_H__
#define CIRCUIT_EXECUTION_H__

template<typename derived_t>
class CircuitExecution { 
public:
	static derived_t * circ_exec;

	block and_gate(const block& in1, const block& in2) {
		return derived().and_gate(in1, in2);
	}
	block xor_gate(const block&in1, const block&in2) {
		return derived().xor_gate(in1, in2);
	}
	block not_gate(const block& in1) {
		return derived().not_gate(in1);
	}
	block public_label(bool b) {
		return derived().public_label(b);
	}	

private:
	derived_t& derived() {
		return *static_cast<derived_t*>(this);
	}
};

template<typename derived_t>
derived_t* CircuitExecution<derived_t>::circ_exec = nullptr;
#endif