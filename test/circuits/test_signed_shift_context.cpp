#include "emp-tool/circuits/signed_int.h"
#include "emp-tool/ir/context/clear.h"

#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

using namespace emp;

template <class F>
static bool dies(F&& f) {
	pid_t pid = fork();
	expecting(pid >= 0, "test_signed_shift_context: fork failed");
	if (pid == 0) {
		alarm(3);
		std::freopen("/dev/null", "w", stderr);
		f();
		_exit(0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	return !(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int main() {
	using I = Int_T<ClearCtx, 8>;
	using U = UInt_T<ClearCtx, 8>;
	bool left = dies([] {
		ClearCtx value_ctx, shift_ctx;
		I value = I::constant(value_ctx, -7);
		U shift = U::constant(shift_ctx, 1);
		(void)(value << shift);
	});
	bool right = dies([] {
		ClearCtx value_ctx, shift_ctx;
		I value = I::constant(value_ctx, -7);
		U shift = U::constant(shift_ctx, 1);
		(void)(value >> shift);
	});
	std::printf("test_signed_shift_context: %s\n",
	            left && right ? "PASS" : "FAILED");
	return left && right ? 0 : 1;
}
