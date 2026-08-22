// core/block_vector.h — aligned block storage without redundant zero-fill.
// Read example() first; the rest is verification.
//
// What's in block_vector.h:
//   default_init_allocator<T>  default-initialize storage for overwrite-first use
//   default_init_vector<T>     vector using that allocator
//   BlockVec                   aligned, overwrite-first block vector

#include "emp-tool/runtime/core/block_vector.h"

#include <cstdint>
#include <iostream>
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

static bool run_correctness() {
	static_assert(is_same_v<BlockVec::value_type, block>);
	bool alignment = check_alignment();
	bool lifetime = check_copy_move_reserve_resize();
	bool nontrivial = check_nontrivial_default_construction();
	cout << "  block alignment is preserved       " << (alignment ? "OK" : "FAIL") << "\n";
	cout << "  copy/move/reserve/resize are sound " << (lifetime ? "OK" : "FAIL") << "\n";
	cout << "  nontrivial default ctor runs       " << (nontrivial ? "OK" : "FAIL") << "\n";
	return alignment && lifetime && nontrivial;
}

int main() {
	example();
	cout << "\n=== correctness ===\n";
	return run_correctness() ? 0 : 1;
}
