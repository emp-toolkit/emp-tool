// circuits/circuit_file.h — Bristol-format parser. Read example() first; the
// rest exercises malformed inputs to verify the parser rejects them rather
// than walking off the end of stack buffers or vectors.
//
// What's in circuit_file.h:
//   BristolFormat       two-input legacy Bristol parser/writer
//   BristolFashion      unified Bristol-Fashion parser
//   execute_circuit     plaintext gate evaluator (consumed by compute<Wire>)
//
// Hardening focus (audit High-1/High-2):
//   - %s scans are width-limited (no stack smash on long gate-type tokens).
//   - Header counts must be non-negative and self-consistent.
//   - Every wire index must satisfy 0 <= idx < num_wire.
//   - Gate arity ∈ {1, 2}; type token must start with 'A'/'X' for arity 2.
//
// All malformed inputs throw std::runtime_error from the constructor —
// callers can recover. ASan/UBSan jobs over this test should stay clean.

#include "emp-tool/emp-tool.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
using namespace emp;

// tmpfile() gives us a writable+readable FILE* with no on-disk path. Write
// the bytes, rewind, hand to the parser. Caller closes.
static FILE *memfile(const std::string &s) {
	FILE *f = tmpfile();
	if (!f) error("tmpfile failed");
	if (fwrite(s.data(), 1, s.size(), f) != s.size()) error("memfile fwrite");
	rewind(f);
	return f;
}

template <typename Parser>
static bool parse_throws(const std::string &s) {
	FILE *f = memfile(s);
	bool threw = false;
	try { Parser p(f); }
	catch (const std::runtime_error &) { threw = true; }
	catch (...) { /* unexpected exception type — count as not-throw */ }
	fclose(f);
	return threw;
}

template <typename Parser>
static bool parse_ok(const std::string &s) {
	FILE *f = memfile(s);
	bool ok = true;
	try { Parser p(f); } catch (...) { ok = false; }
	fclose(f);
	return ok;
}

// Returns true only if parsing completes without crashing (either throw
// or normal return). Used for the long-token check: %199s caps the read at
// 199 chars + NUL = exactly str[200], so this should NOT overflow. The test
// passes as long as the process is still alive after the call.
static bool parse_no_crash_format(const std::string &s) {
	FILE *f = memfile(s);
	try { BristolFormat p(f); } catch (...) { /* fine */ }
	fclose(f);
	return true;
}

void example() {
	// Minimal well-formed BristolFormat: 1 XOR gate, 3 wires, n1=n2=n3=1.
	// Gate line: "<arity> <fanout> <in0> <in1> <out> <type>"  → "2 1 0 1 2 XOR".
	const std::string ok =
	    "1 3\n"
	    "1 1 1\n"
	    "\n"
	    "2 1 0 1 2 XOR\n";
	FILE *f = memfile(ok);
	BristolFormat bf(f);
	fclose(f);
	std::cout << "example: parsed BristolFormat"
	          << " num_gate=" << bf.num_gate
	          << " num_wire=" << bf.num_wire
	          << " n1/n2/n3=" << bf.n1 << "/" << bf.n2 << "/" << bf.n3
	          << "\n";
}

void run_correctness() {
	bool all = true;
	auto check = [&](const char *name, bool got) {
		std::cout << (got ? "  OK   " : "  FAIL ") << name << "\n";
		all = all && got;
	};

	// BristolFormat: well-formed input parses.
	const std::string ok_format =
	    "1 3\n"
	    "1 1 1\n"
	    "\n"
	    "2 1 0 1 2 XOR\n";
	check("format valid round-trip", parse_ok<BristolFormat>(ok_format));

	// High-2: negative num_gate must be rejected (signed overflow in resize).
	check("format reject negative num_gate",
	      parse_throws<BristolFormat>("-1 3\n0 0 0\n\n"));

	// High-2: negative num_wire likewise.
	check("format reject negative num_wire",
	      parse_throws<BristolFormat>("1 -3\n1 1 1\n\n"));

	// High-2: wire index >= num_wire is an OOB read into wires[] downstream.
	check("format reject oob wire idx",
	      parse_throws<BristolFormat>(
	          "1 3\n"
	          "1 1 1\n"
	          "\n"
	          "2 1 0 1 99 XOR\n"));

	// High-2: wire index < 0.
	check("format reject negative wire idx",
	      parse_throws<BristolFormat>(
	          "1 3\n"
	          "1 1 1\n"
	          "\n"
	          "2 1 0 -1 2 XOR\n"));

	// High-1: pre-fix this overflowed str[200] on the stack. With %199s the
	// parser now reads at most 199 bytes into a 200-byte buffer. We accept
	// any outcome (throw or success) — the property under test is "no UB".
	{
		std::string huge(500, 'A');
		std::string input = "1 3\n1 1 1\n\n2 1 0 1 2 " + huge + "\n";
		check("format 500-char type token survives without UB",
		      parse_no_crash_format(input));
	}

	// High-2: unknown gate type must throw (previously left gates[4*i+3]
	// uninitialized → assert(false) in Debug, UB in Release).
	check("format reject unknown gate type",
	      parse_throws<BristolFormat>(
	          "1 3\n"
	          "1 1 1\n"
	          "\n"
	          "2 1 0 1 2 ZZZ\n"));

	// High-2: unsupported arity.
	check("format reject arity 3",
	      parse_throws<BristolFormat>(
	          "1 4\n"
	          "1 1 1\n"
	          "\n"
	          "3 1 0 1 2 3 AND\n"));

	// Short reads on the header throw rather than reading uninitialized
	// ints (and resizing the gates vector to a junk size).
	check("format reject short header",
	      parse_throws<BristolFormat>("5\n"));

	// BristolFashion: well-formed input parses (1 XOR over 3 wires,
	// one input vector of width 2, one output vector of width 1).
	const std::string ok_fashion =
	    "1 3\n"
	    "1 2\n"
	    "1 1\n"
	    "2 1 0 1 2 XOR\n";
	check("fashion valid round-trip", parse_ok<BristolFashion>(ok_fashion));

	// High-2: input-vector widths summing past num_wire must be rejected.
	check("fashion reject input widths exceed num_wire",
	      parse_throws<BristolFashion>(
	          "0 2\n"
	          "1 99\n"
	          "1 1\n"));

	// High-2: output-vector widths past num_wire likewise.
	check("fashion reject output widths exceed num_wire",
	      parse_throws<BristolFashion>(
	          "0 2\n"
	          "1 1\n"
	          "1 99\n"));

	// High-2: OOB wire index in BristolFashion gate.
	check("fashion reject oob wire idx",
	      parse_throws<BristolFashion>(
	          "1 3\n"
	          "1 2\n"
	          "1 1\n"
	          "2 1 0 1 99 AND\n"));

	if (!all) error("circuit_file: malformed input not rejected");
}

void bench(double /*sec*/) {
	// No perf surface — parser is only called at protocol setup. The
	// hardening adds a handful of comparisons per gate, dwarfed by I/O.
}

int main(int argc, char **argv) {
	double sec = (argc > 1) ? atof(argv[1]) : 0.2;
	(void)sec;
	example();
	run_correctness();
	bench(sec);
	return 0;
}
