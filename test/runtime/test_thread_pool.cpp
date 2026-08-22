// third_party/ThreadPool.h — fixed-size worker pool with future-returning tasks.
// Read example() first; the rest is verification.
//
// What's in ThreadPool.h:
//   ThreadPool(n)       start n workers
//   enqueue(f, args...) schedule one task and return its future
//   size()              report the fixed worker count

#include "emp-tool/third_party/ThreadPool.h"

#include <atomic>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>
#include <vector>

using namespace std;

template <class F>
static bool dies(F&& f) {
	pid_t pid = fork();
	if (pid == 0) {
		close(STDERR_FILENO);
		f();
		_exit(0);
	}
	if (pid < 0) return false;
	int status = 0;
	if (waitpid(pid, &status, 0) != pid) return false;
	return !WIFEXITED(status) || WEXITSTATUS(status) != 0;
}

static void example() {
	ThreadPool pool(2);
	auto answer = pool.enqueue([] { return 6 * 7; });
	cout << "ThreadPool task result = " << answer.get() << "\n";
}

static bool check_tasks_complete() {
	ThreadPool pool(3);
	vector<future<int>> results;
	for (int i = 0; i < 32; ++i)
		results.push_back(pool.enqueue([i] { return i * i; }));
	for (int i = 0; i < 32; ++i)
		if (results[i].get() != i * i) return false;
	return pool.size() == 3;
}

static bool check_destructor_drains_queue() {
	atomic<int> completed{0};
	{
		ThreadPool pool(2);
		for (int i = 0; i < 32; ++i)
			pool.enqueue([&completed] { completed.fetch_add(1); });
	}
	return completed.load() == 32;
}

static bool check_move_only_argument() {
	ThreadPool pool(1);
	auto value = make_unique<int>(41);
	auto result = pool.enqueue(
	    [](unique_ptr<int> input) { return *input + 1; }, std::move(value));
	return value == nullptr && result.get() == 42;
}

static int increment(int& value) { return ++value; }

struct LvalueCallable {
	int operator()() & { return 17; }
};

struct CategoryOverload {
	int operator()(int& value) { return value + 1; }
	string operator()(int&&) { return "wrong overload"; }
};

struct NotCallable {};

template <class F>
concept PoolEnqueueable = requires(ThreadPool& pool, F&& fn) {
	pool.enqueue(std::forward<F>(fn));
};

static_assert(!PoolEnqueueable<NotCallable>);

static bool check_legacy_invocation() {
	ThreadPool pool(1);
	int value = 1;
	auto by_reference = pool.enqueue(increment, value);
	LvalueCallable callable;
	auto lvalue_callable = pool.enqueue(callable);
	return by_reference.get() == 2 && value == 1 &&
	       lvalue_callable.get() == 17;
}

static bool check_result_matches_stored_invocation() {
	ThreadPool pool(1);
	auto result = pool.enqueue(CategoryOverload{}, 41);
	static_assert(is_same_v<decltype(result), future<int>>);
	return result.get() == 42;
}

static bool run_correctness() {
	bool tasks = check_tasks_complete();
	bool drains = check_destructor_drains_queue();
	bool move_only = check_move_only_argument();
	bool legacy = check_legacy_invocation();
	bool stored_result = check_result_matches_stored_invocation();
	bool rejects_zero = dies([] { ThreadPool pool(0); });
	cout << "  tasks complete and size is fixed  " << (tasks ? "OK" : "FAIL") << "\n";
	cout << "  destructor drains queued work     " << (drains ? "OK" : "FAIL") << "\n";
	cout << "  move-only arguments are supported " << (move_only ? "OK" : "FAIL") << "\n";
	cout << "  legacy invocation is preserved    " << (legacy ? "OK" : "FAIL") << "\n";
	cout << "  stored invocation type is exact   " << (stored_result ? "OK" : "FAIL") << "\n";
	cout << "  zero workers are rejected         " << (rejects_zero ? "OK" : "FAIL") << "\n";
	return tasks && drains && move_only && legacy && stored_result && rejects_zero;
}

int main() {
	example();
	cout << "\n=== correctness ===\n";
	return run_correctness() ? 0 : 1;
}
