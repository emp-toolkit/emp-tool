// Context checks at the integer operator boundary, built with
// EMP_CONTEXT_CHECKS=1: an operand from another context stops the process, and
// the same operation over one context computes as usual.
#include "emp-tool/circuits/signed_int.h"
#include "emp-tool/circuits/unsigned_int.h"
#include "emp-tool/ir/context/clear.h"

#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

using namespace emp;

static int bad = 0;
static void chk(const char* what, bool ok) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", what);
		++bad;
	}
}

// The child ends through error(), which exits with status 1.
template <class F>
static bool stops(F&& f) {
	pid_t pid = fork();
	expecting(pid >= 0, "test_int_context: fork failed");
	if (pid == 0) {
		alarm(3);
		std::freopen("/dev/null", "w", stderr);
		f();
		_exit(0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) && WEXITSTATUS(status) == 1;
}

int main() {
	using I = Int_T<ClearCtx, 8>;
	using U = UInt_T<ClearCtx, 8>;
	using DU = DynamicUInt_T<ClearCtx>;
	using DI = DynamicInt_T<ClearCtx>;

	chk("signed << stops on a foreign shift amount", stops([] {
		ClearCtx value_ctx, shift_ctx;
		(void)(I::constant(value_ctx, -7) << U::constant(shift_ctx, 1));
	}));
	chk("signed >> stops on a foreign shift amount", stops([] {
		ClearCtx value_ctx, shift_ctx;
		(void)(I::constant(value_ctx, -7) >> U::constant(shift_ctx, 1));
	}));
	chk("unsigned << stops on a foreign shift amount", stops([] {
		ClearCtx value_ctx, shift_ctx;
		(void)(U::constant(value_ctx, 7) << U::constant(shift_ctx, 1));
	}));
	chk("dynamic unsigned + stops on a foreign operand", stops([] {
		ClearCtx a_ctx, b_ctx;
		(void)(DU::constant(a_ctx, 8, 7) + DU::constant(b_ctx, 8, 1));
	}));
	chk("dynamic signed == stops on a foreign operand", stops([] {
		ClearCtx a_ctx, b_ctx;
		(void)(DI::constant(a_ctx, 8, -7) == DI::constant(b_ctx, 8, -7));
	}));
	chk("dynamic select stops on a foreign condition", stops([] {
		ClearCtx value_ctx, bit_ctx;
		const DU value = DU::constant(value_ctx, 8, 7);
		(void)value.select(Bit_T<ClearCtx>::constant(bit_ctx, true), value);
	}));

	ClearCtx ctx;
	const I minus_seven = I::constant(ctx, -7);
	const U one = U::constant(ctx, 1);
	chk("signed << over one context",
	    (((minus_seven << one) == I::constant(ctx, -14)).w & 1) != 0);
	chk("signed >> over one context",
	    (((minus_seven >> one) == I::constant(ctx, -4)).w & 1) != 0);
	const DU seven = DU::constant(ctx, 8, 7), dyn_one = DU::constant(ctx, 8, 1);
	chk("dynamic + over one context",
	    (((seven + dyn_one) == DU::constant(ctx, 8, 8)).w & 1) != 0);

	std::printf("test_int_context: %s\n", bad ? "FAILED" : "PASS");
	return bad ? 1 : 0;
}
