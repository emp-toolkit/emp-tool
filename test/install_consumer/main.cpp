// Exercises an installed emp-tool through its public umbrella header: a core
// primitive (PRG over block), and a circuit asset resolved through the
// installed asset directory (the path baked at install time). If find_package
// wired the target correctly and the assets installed, this builds and prints
// OK; the release-package CI job asserts on that.
#include "emp-tool/emp-tool.h"

#include <cstdio>

int main() {
	// Core: a PRG fill + a GF(2^128) multiply, enough to pull in block/AES/f2k.
	emp::block a, b, c;
	emp::PRG().random_block(&a, 1);
	emp::PRG().random_block(&b, 1);
	emp::gfmul(a, b, &c);

	// Installed circuit asset: resolve one shipped .empbc through the installed
	// asset directory and load it (load_empbc_file validates on decode).
	const std::string path = emp::circuit::find_circuit_asset("fp32_add.empbc");
	emp::circuit::BooleanProgram prog = emp::circuit::load_empbc_file(path.c_str());

	std::printf("install_consumer: OK (fp32_add: %u inputs, %u gates)\n",
	            prog.num_inputs, (unsigned)prog.gates.size());
	return 0;
}
