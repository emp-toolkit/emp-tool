// Compile-and-run gate for the exception-free public surface
// (docs/api_conventions.md "error(), never exceptions"). This TU compiles
// with -fno-exceptions (see test/CMakeLists.txt), so any `throw` reachable
// from the public umbrella fails the BUILD, not just the run. The body
// instantiates the two paths that historically threw: ThreadPool::enqueue
// (vendored, patched to emp::error) and PRG system-entropy seeding
// (std::random_device, replaced by getentropy). It also instantiates the
// exception-disabled future-draining helpers.
#include "emp-tool/emp-tool.h"
#include <cstdio>
using namespace emp;

int main() {
	PRG prg;                       // system-entropy seeding path
	block b;
	prg.random_block(&b, 1);

	ThreadPool pool(2);            // enqueue instantiation forces the template
	auto fut = pool.enqueue([] { return 7; });
	if (fut.get() != 7) {
		std::printf("test_no_exceptions: pool result mismatch\n");
		return 1;
	}
	std::vector<std::future<void>> empty;
	joinNclean(empty);
	std::vector<std::future<bool>> empty_cheat;
	if (joinNcleanCheat(empty_cheat)) return 1;
	std::printf("test_no_exceptions: OK\n");
	return 0;
}
