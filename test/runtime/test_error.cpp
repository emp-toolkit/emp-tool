// core/error.h — fatal error reporting and expected-condition branch hints.
// Read example() first; the rest is verification.
//
// What's in error.h:
//   expecting(condition, message)  enforce a likely-true condition
//   expecting(condition, factory)  lazily build a dynamic failure message

#include "emp-tool/runtime/core/error.h"

#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using namespace emp;
using namespace std;

struct DeathResult {
	bool died;
	string stderr_text;
};

template <class F>
static DeathResult run_child(F&& f) {
	int stderr_pipe[2];
	if (pipe(stderr_pipe) != 0) return {false, {}};
	pid_t pid = fork();
	if (pid == 0) {
		close(stderr_pipe[0]);
		dup2(stderr_pipe[1], STDERR_FILENO);
		close(stderr_pipe[1]);
		f();
		_exit(0);
	}
	close(stderr_pipe[1]);
	if (pid < 0) {
		close(stderr_pipe[0]);
		return {false, {}};
	}
	string stderr_text;
	char buf[256];
	ssize_t nread;
	while ((nread = read(stderr_pipe[0], buf, sizeof(buf))) > 0)
		stderr_text.append(buf, static_cast<size_t>(nread));
	close(stderr_pipe[0]);
	int st = 0;
	waitpid(pid, &st, 0);
	return {!(WIFEXITED(st) && WEXITSTATUS(st) == 0), stderr_text};
}

static void example() {
	int count = 3;
	expecting(count >= 0, "negative count");
	cout << "expecting(count >= 0, ...) passed\n";
}

static bool check_expecting_evaluates_once() {
	int evaluations = 0;
	expecting(++evaluations == 1, "condition evaluated more than once");
	return evaluations == 1;
}

static bool check_lazy_message_stays_lazy() {
	bool built = false;
	expecting(true, [&] {
		built = true;
		return string("unexpected lazy message");
	});
	return !built;
}

static bool check_buffered_diagnostic_is_flushed() {
	DeathResult result = run_child([] {
		char buffer[4096];
		if (setvbuf(stderr, buffer, _IOFBF, sizeof(buffer)) != 0) _exit(2);
		expecting(false, "buffered fatal diagnostic");
	});
	return result.died &&
	       result.stderr_text.find("buffered fatal diagnostic") != string::npos;
}

static bool run_correctness() {
	bool once = check_expecting_evaluates_once();
	bool lazy = check_lazy_message_stays_lazy();
	bool buffered = check_buffered_diagnostic_is_flushed();
	DeathResult expectation =
		run_child([] { expecting(false, "expected expectation failure"); });
	DeathResult dynamic = run_child([] {
		expecting(false, [] { return string("expected lazy failure"); });
	});
	bool expectation_message =
		expectation.stderr_text.find("expected expectation failure") != string::npos;
	bool caller_location = expectation.stderr_text.find("test_error.cpp:") != string::npos;
	bool dynamic_message = dynamic.stderr_text.find("expected lazy failure") != string::npos;
	cout << "  expecting evaluates once          " << (once ? "OK" : "FAIL") << "\n";
	cout << "  lazy message stays lazy           " << (lazy ? "OK" : "FAIL") << "\n";
	cout << "  buffered diagnostic is flushed   " << (buffered ? "OK" : "FAIL") << "\n";
	cout << "  failed expectation terminates     " << (expectation.died ? "OK" : "FAIL") << "\n";
	cout << "  expectation reports its message   " << (expectation_message ? "OK" : "FAIL") << "\n";
	cout << "  expectation reports caller site   " << (caller_location ? "OK" : "FAIL") << "\n";
	cout << "  lazy failure reports/terminates   " << (dynamic.died && dynamic_message ? "OK" : "FAIL") << "\n";
	return once && lazy && buffered && expectation.died && expectation_message && caller_location &&
	       dynamic.died && dynamic_message;
}

int main() {
	example();
	cout << "\n=== correctness ===\n";
	return run_correctness() ? 0 : 1;
}
