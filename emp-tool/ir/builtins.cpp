// Process-wide caches for the on-disk boolean-circuit builtins (crypto + float),
// see ir/builtins.h. Each is loaded once via the .empbc loader and cached.

#include "emp-tool/ir/builtins.h"
#include "emp-tool/ir/assets.h"
#include "emp-tool/ir/empbc.h"
#include "emp-tool/runtime/core/utils.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace emp {
namespace circuit {

namespace {

struct CircuitSignature {
	uint32_t inputs;
	uint32_t outputs;
};

const BooleanProgram& checked_circuit(const std::string& key,
		CircuitSignature signature) {
	static std::map<std::string, BooleanProgram> cache;
	static std::mutex mtx;
	auto check_signature = [&](const BooleanProgram& prog) {
		expecting(prog.num_inputs == signature.inputs &&
		          prog.outputs.size() == signature.outputs, [&] {
			return "builtin circuit " + key + " has signature " +
			       std::to_string(prog.num_inputs) + " -> " +
			       std::to_string(prog.outputs.size()) + ", expected " +
			       std::to_string(signature.inputs) + " -> " +
			       std::to_string(signature.outputs);
		});
	};

	std::lock_guard<std::mutex> lk(mtx);
	auto it = cache.find(key);
	if (it != cache.end()) return it->second;

	std::string asset = key + ".empbc";
	std::string path = find_circuit_asset(asset);
	BooleanProgram prog = load_empbc_file(path.c_str());
	check_signature(prog);
	return cache.emplace(key, std::move(prog)).first->second;
}

CircuitSignature builtin_signature(std::string_view name) {
	if (name == "aes128") return {256, 128};
	if (name == "sha256_256" || name == "sha3_256_256") return {256, 256};
	expecting(false, "builtin_circuit: unknown builtin name");
	return {};
}

CircuitSignature float_signature(int width, std::string_view op) {
	expecting(width == 16 || width == 32 || width == 64,
	          "float_circuit: width must be 16, 32, or 64");

	if (op == "square" || op == "sqrt" || op == "recip" || op == "rsqrt")
		return {(uint32_t)width, (uint32_t)width};
	if (op == "add" || op == "sub" || op == "mul" || op == "div" ||
	    op == "min" || op == "max")
		return {(uint32_t)(2 * width), (uint32_t)width};
	if (op == "fma")
		return {(uint32_t)(3 * width), (uint32_t)width};
	if (op == "isnan" || op == "isinf" || op == "iszero")
		return {(uint32_t)width, 8};
	if (op == "eq" || op == "ne" || op == "lt" || op == "le" ||
	    op == "gt" || op == "ge")
		return {(uint32_t)(2 * width), 8};

	expecting(false, "float_circuit: unknown operation");
	return {};
}

}  // namespace

const BooleanProgram& builtin_circuit(const char* name) {
	expecting(name != nullptr && name[0] != '\0',
	          "builtin_circuit: empty builtin name");
	std::string key(name);
	return checked_circuit(key, builtin_signature(key));
}

const BooleanProgram& float_circuit(int width, const char* op) {
	expecting(op != nullptr && op[0] != '\0',
	          "float_circuit: empty operation");
	std::string key = "fp" + std::to_string(width) + "_" + op;
	return checked_circuit(key, float_signature(width, op));
}

}  // namespace circuit
}  // namespace emp
