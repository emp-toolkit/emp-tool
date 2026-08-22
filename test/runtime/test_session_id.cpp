// crypto/session_id.h — deterministic, session-bound child identifiers. Read
// example() first; the rest is verification.
//
// What's in session_id.h:
//   SessionID(sid)  seed a child-derivation stream
//   value()         read the current session identifier
//   derive()        return AES_sid(counter) and advance the child counter

#include "emp-tool/runtime/crypto/session_id.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <set>

using namespace emp;
using namespace std;

static void example() {
	SessionID parent(makeBlock(0, 7));
	SessionID child = parent.derive();
	cout << "SessionID child = " << child.value() << '\n';
}

static block reference_child(block sid, uint64_t counter) {
	AES_KEY key;
	AES_set_encrypt_key(sid, &key);
	block child = makeBlock(0, counter);
	AES_ecb_encrypt_blks<1>(&child, &key);
	return child;
}

static array<uint8_t, 16> bytes(block value) {
	array<uint8_t, 16> out;
	memcpy(out.data(), &value, sizeof(value));
	return out;
}

static bool check_reference_derivation() {
	const block root = makeBlock(0x0123456789abcdefULL, 0xfedcba9876543210ULL);
	SessionID sid(root);
	for (uint64_t counter = 0; counter < 32; ++counter) {
		const block expected = reference_child(root, counter);
		const block actual = sid.derive().value();
		if (!cmpBlock(&actual, &expected, 1)) return false;
	}
	return true;
}

static bool check_sequential_uniqueness() {
	SessionID sid(makeBlock(0x1020304050607080ULL, 0x90a0b0c0d0e0f000ULL));
	set<array<uint8_t, 16>> children;
	for (int i = 0; i < 256; ++i)
		children.insert(bytes(sid.derive().value()));
	return children.size() == 256;
}

static bool check_endpoint_copy_determinism() {
	SessionID endpoint_a(makeBlock(0xabcdef0123456789ULL, 0x9876543210fedcbaULL));
	for (int i = 0; i < 7; ++i) (void)endpoint_a.derive();
	SessionID endpoint_b = endpoint_a;

	for (int i = 0; i < 64; ++i) {
		const block a = endpoint_a.derive().value();
		const block b = endpoint_b.derive().value();
		if (!cmpBlock(&a, &b, 1)) return false;
	}
	return true;
}

static bool check_zero_seed() {
	SessionID implicit_zero;
	SessionID explicit_zero(zero_block);
	const block implicit_value = implicit_zero.value();
	const block explicit_value = explicit_zero.value();
	if (!cmpBlock(&implicit_value, &explicit_value, 1)) return false;

	for (uint64_t counter = 0; counter < 16; ++counter) {
		const block expected = reference_child(zero_block, counter);
		const block implicit_child = implicit_zero.derive().value();
		const block explicit_child = explicit_zero.derive().value();
		if (!cmpBlock(&implicit_child, &expected, 1) ||
		    !cmpBlock(&explicit_child, &expected, 1))
			return false;
	}
	return true;
}

static bool run_correctness() {
	bool reference = check_reference_derivation();
	bool unique = check_sequential_uniqueness();
	bool copies = check_endpoint_copy_determinism();
	bool zero = check_zero_seed();
	cout << "  AES reference derivation     " << (reference ? "OK" : "FAIL") << '\n';
	cout << "  sequential children unique   " << (unique ? "OK" : "FAIL") << '\n';
	cout << "  endpoint copies deterministic " << (copies ? "OK" : "FAIL") << '\n';
	cout << "  default zero seed defined     " << (zero ? "OK" : "FAIL") << '\n';
	return reference && unique && copies && zero;
}

int main() {
	example();
	cout << "\n=== correctness ===\n";
	bool ok = run_correctness();
	return ok ? 0 : 1;
}
