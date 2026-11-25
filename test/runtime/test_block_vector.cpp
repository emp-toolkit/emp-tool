// core/block_vector.h — aligned block storage without redundant zero-fill.
// Read example() first; the rest is verification.
//
// What's in block_vector.h:
//   default_init_allocator<T>  default-initialize storage for overwrite-first use;
//                              destroying trivial elements does nothing; large
//                              allocations ask for transparent huge pages
//   default_init_vector<T>     vector using that allocator
//   BlockVec                   aligned, overwrite-first block vector
//   advise_huge_pages          the huge-page request (MADV_HUGEPAGE on Linux)

#include "emp-tool/runtime/core/block_vector.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

using namespace emp;
using namespace std;

static bool blocks_equal(block lhs, block rhs) {
	return cmpBlock(&lhs, &rhs, 1);
}

static void example() {
	BlockVec blocks(2);
	blocks[0] = makeBlock(0, 1);
	blocks[1] = makeBlock(0, 2);
	cout << "BlockVec size = " << blocks.size()
	     << ", first = " << blocks[0] << "\n";
}

static bool check_alignment() {
	for (size_t size : {size_t{1}, size_t{3}, size_t{257}}) {
		BlockVec blocks(size);
		if (reinterpret_cast<uintptr_t>(blocks.data()) % alignof(block) != 0)
			return false;
	}
	return true;
}

static bool check_copy_move_reserve_resize() {
	BlockVec original(4);
	for (size_t i = 0; i < original.size(); ++i)
		original[i] = makeBlock(i + 10, i + 1);

	BlockVec copy = original;
	BlockVec moved = std::move(copy);
	moved.reserve(32);
	for (size_t i = 0; i < original.size(); ++i)
		if (!blocks_equal(moved[i], original[i])) return false;

	moved.resize(8);
	for (size_t i = 4; i < moved.size(); ++i)
		moved[i] = makeBlock(i + 10, i + 1);
	for (size_t i = 0; i < moved.size(); ++i)
		if (!blocks_equal(moved[i], makeBlock(i + 10, i + 1))) return false;

	moved.resize(2);
	return moved.size() == 2 &&
	       blocks_equal(moved[0], makeBlock(10, 1)) &&
	       blocks_equal(moved[1], makeBlock(11, 2));
}

struct NonTrivial {
	NonTrivial() : value(23) { ++constructions; }
	static inline int constructions = 0;
	int value;
};

static bool check_nontrivial_default_construction() {
	NonTrivial::constructions = 0;
	default_init_vector<NonTrivial> values(3);
	return NonTrivial::constructions == 3 && values[0].value == 23 &&
	       values[1].value == 23 && values[2].value == 23;
}

// A base allocator that checks, when the vector hands its storage back, that every
// byte still holds kFill: destroying block elements must not write them.
static constexpr unsigned char kFill = 0xA5;
static bool g_storage_intact = false;

__attribute__((noinline)) static bool all_bytes_are(const void *p, size_t n, unsigned char v) {
	const volatile unsigned char *b = static_cast<const volatile unsigned char *>(p);
	for (size_t i = 0; i < n; ++i)
		if (b[i] != v) return false;
	return true;
}

template <typename T>
struct CheckingAllocator : std::allocator<T> {
	using value_type = T;
	template <typename U>
	struct rebind { using other = CheckingAllocator<U>; };
	CheckingAllocator() = default;
	template <typename U>
	CheckingAllocator(const CheckingAllocator<U> &) noexcept {}
	void deallocate(T *p, size_t n) {
		g_storage_intact = all_bytes_are(p, n * sizeof(T), kFill);
		std::allocator<T>::deallocate(p, n);
	}
};

// Destroying a vector of blocks leaves its elements as they were until the storage is
// freed; the same holds for the elements a shrinking resize drops.
static bool check_trivial_destruction_writes_nothing() {
	using Vec = std::vector<block, default_init_allocator<block, CheckingAllocator<block>>>;
	const block fill = makeBlock(0xA5A5A5A5A5A5A5A5ULL, 0xA5A5A5A5A5A5A5A5ULL);
	g_storage_intact = false;
	{
		Vec blocks(1024);
		for (auto &b : blocks) b = fill;
		blocks.resize(16);
		if (!all_bytes_are(blocks.data() + 16, (1024 - 16) * sizeof(block), kFill)) return false;
	}
	return g_storage_intact;
}

#if defined(__linux__) && defined(MADV_HUGEPAGE)
// The VmFlags of the /proc/self/smaps mapping that holds address p, or "" when
// there is no such file or mapping.
static string vm_flags_of(const void *p) {
	ifstream smaps("/proc/self/smaps");
	const uintptr_t at = reinterpret_cast<uintptr_t>(p);
	string line;
	bool inside = false;
	while (getline(smaps, line)) {
		uintptr_t lo = 0, hi = 0;
		char dash = 0;
		istringstream head(line);
		if (head >> hex >> lo >> dash >> hi && dash == '-') {   // a mapping's first line
			inside = lo <= at && at < hi;
			continue;
		}
		if (inside && line.rfind("VmFlags:", 0) == 0) return line;
	}
	return "";
}
#endif

// A vector of at least kHugePageAdviceBytes holds its contents like any other, and on
// Linux with transparent huge pages its mapping carries the advice (VmFlags "hg").
static bool check_huge_page_advice() {
	BlockVec big((size_t{4} << 20) / sizeof(block));
	for (size_t i = 0; i < big.size(); ++i) big[i] = makeBlock(i, ~i);
	for (size_t i = 0; i < big.size(); ++i)
		if (!blocks_equal(big[i], makeBlock(i, ~i))) return false;
#if defined(__linux__) && defined(MADV_HUGEPAGE)
	if (ifstream("/sys/kernel/mm/transparent_hugepage/enabled")) {
		const string flags = vm_flags_of(big.data()) + " ";
		if (flags.find(" hg ") == string::npos) return false;
	}
#endif
	return true;
}

static bool run_correctness() {
	static_assert(is_same_v<BlockVec::value_type, block>);
	bool alignment = check_alignment();
	bool lifetime = check_copy_move_reserve_resize();
	bool nontrivial = check_nontrivial_default_construction();
	bool untouched = check_trivial_destruction_writes_nothing();
	bool huge = check_huge_page_advice();
	cout << "  block alignment is preserved       " << (alignment ? "OK" : "FAIL") << "\n";
	cout << "  copy/move/reserve/resize are sound " << (lifetime ? "OK" : "FAIL") << "\n";
	cout << "  nontrivial default ctor runs       " << (nontrivial ? "OK" : "FAIL") << "\n";
	cout << "  destroying blocks writes nothing   " << (untouched ? "OK" : "FAIL") << "\n";
	cout << "  large vectors ask for huge pages   " << (huge ? "OK" : "FAIL") << "\n";
	return alignment && lifetime && nontrivial && untouched && huge;
}

int main() {
	example();
	cout << "\n=== correctness ===\n";
	return run_correctness() ? 0 : 1;
}
