// core/deterministic_mode.h — deterministic (party, lane, ordinal) randomness for
// wire-byte equivalence. Read example() first; the rest is verification. In a
// normal build this test checks that the deterministic paths are compiled out;
// the full checks run in a deterministic-mode build (-DEMP_DETERMINISTIC_MODE=ON).
//
// What's in deterministic_mode.h:
//   is_deterministic_mode()                true in a deterministic-mode build
//   set_deterministic_party(p)             this process draws party p's seeds (parse_party)
//   next_deterministic_seed()              (party, lane, ordinal) consumed by PRG()
//   deterministic_lane_scope guard(lane)   RAII lane for the body of spawned work
//   next_deterministic_child_lane()        creator-side deterministic lane derivation
//   reset_deterministic_seeds()            rewind every lane's ordinal, release lane 0
//
// The determinism model: seeds depend only on (which party, which logical
// lane, which draw within it), never on cross-thread scheduling.
// ThreadPool::enqueue derives a lane per task on the enqueuing thread and
// installs it around the task body, so pool-parallel randomness reproduces
// across runs and across pool re-creations.

#include "emp-tool/emp-tool.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace emp;
using namespace std;

template <class F>
static int run_child(F&& f) {
	pid_t pid = fork();
	if (pid == 0) {
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
		f();
		_exit(0);
	}
	if (pid < 0) return -1;
	int status = 0;
	if (waitpid(pid, &status, 0) != pid) return -1;
	return status;
}

static bool child_died(int status) {
	return status >= 0 &&
	       (!WIFEXITED(status) || WEXITSTATUS(status) != 0);
}

static bool child_succeeded(int status) {
	return status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// Runs f in a forked child with stdout closed and stderr captured into *err.
// Returns whether the child exited 0.
template <class F>
static bool run_child_capturing_stderr(F&& f, string* err) {
	int fds[2];
	if (pipe(fds) != 0) return false;
	pid_t pid = fork();
	if (pid == 0) {
		close(fds[0]);
		dup2(fds[1], STDERR_FILENO);
		close(fds[1]);
		close(STDOUT_FILENO);
		f();
		_exit(0);
	}
	close(fds[1]);
	char buf[256];
	ssize_t n;
	while (pid > 0 && (n = read(fds[0], buf, sizeof(buf))) > 0)
		err->append(buf, static_cast<size_t>(n));
	close(fds[0]);
	int status = 0;
	return pid > 0 && waitpid(pid, &status, 0) == pid && child_succeeded(status);
}

// The first two default PRGs of a fresh process, in a forked child with
// EMP_DETERMINISTIC_MODE=1 (and the old name EMP_TEST_MODE=1) in its environment.
// A deterministic-mode build must give the main lane's (0,0) and (0,1) seeds and
// print the insecurity banner exactly once; a normal build must ignore the
// variables, draw real seeds, and print nothing.
// main() probes before this process draws anything, because the child
// inherits the parent's lane state and banner flag.
static bool probe_first_draws() {
	string err;
	bool ok = run_child_capturing_stderr([] {
		if (setenv("EMP_DETERMINISTIC_MODE", "1", 1) != 0 || setenv("EMP_TEST_MODE", "1", 1) != 0)
			_exit(1);
		PRG a, b;
		const block e0 = makeBlock(0, 0), e1 = makeBlock(0, 1);
		const block s0 = a.seed(), s1 = b.seed();
		const bool deterministic = cmpBlock(&e0, &s0, 1) && cmpBlock(&e1, &s1, 1);
		if (deterministic != is_deterministic_mode() || cmpBlock(&s0, &s1, 1)) _exit(1);
	}, &err);
	const string banner = "WARNING: EMP DETERMINISTIC-MODE BUILD";
	const size_t first = err.find(banner);
	if constexpr (is_deterministic_mode())
		return ok && first != string::npos && err.find(banner, first + 1) == string::npos;
	else
		return ok && first == string::npos;
}

static bool check_first_draws(bool probed) {
	cout << "  [first draws: " << (is_deterministic_mode() ? "(0,0), (0,1), one banner"
	                                              : "real seeds, environment ignored")
	     << "]  " << (probed ? "OK" : "FAIL") << "\n";
	return probed;
}


// ---------- example ----------

static void example() {
	cout << "=== example ===\n";
	reset_deterministic_seeds();

	// (1) Main thread is lane 0; ordinals count from 0.
	PRG a, b;
	cout << "  first  PRG() seed = " << a.seed() << "  (= makeBlock(0, 0))\n";
	cout << "  second PRG() seed = " << b.seed() << "  (= makeBlock(0, 1))\n";

	// (2) Spawned work runs under a creator-chosen lane.
	{
		deterministic_lane_scope guard(7);
		PRG c;
		cout << "  lane-7 PRG() seed = " << c.seed() << "  (= makeBlock(7, 0))\n";
	}

	// (3) Reset rewinds every lane.
	reset_deterministic_seeds();
	PRG d;
	cout << "  after reset       = " << d.seed() << "  (= makeBlock(0, 0) again)\n";
}

// ---------- correctness ----------

static bool expect_seed(const PRG& p, uint64_t lane, uint64_t ordinal, const char* what) {
	block e = makeBlock((int64_t)lane, (int64_t)ordinal);
	block s = p.seed();
	bool ok = cmpBlock(&e, &s, 1);
	if (!ok)
		cout << "  [" << what << "]  FAIL  seed=" << s << " expected=" << e << "\n";
	return ok;
}

static bool check_main_lane_sequence() {
	reset_deterministic_seeds();
	PRG a, b;
	bool ok = expect_seed(a, 0, 0, "main lane") && expect_seed(b, 0, 1, "main lane");

	// The ctor id XORs into the low (ordinal) half, as for any seed.
	PRG c(nullptr, 5);
	block e = makeBlock(0, 7);  // ordinal 2 XOR id 5
	block s = c.seed();
	ok &= cmpBlock(&e, &s, 1);

	// Reset rewinds the ordinal.
	reset_deterministic_seeds();
	PRG d;
	ok &= expect_seed(d, 0, 0, "main lane after reset");

	cout << "  [main lane: (0,0), (0,1), id-XOR, reset]  " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_lane_scope_nesting() {
	reset_deterministic_seeds();
	PRG a;                       // (0, 0)
	bool ok = expect_seed(a, 0, 0, "before scope");
	{
		deterministic_lane_scope outer(42);
		PRG b, c;                // (42, 0), (42, 1)
		ok &= expect_seed(b, 42, 0, "outer scope") && expect_seed(c, 42, 1, "outer scope");
		{
			deterministic_lane_scope inner(43);
			PRG d;               // (43, 0)
			ok &= expect_seed(d, 43, 0, "inner scope");
		}
		PRG e;                   // (42, 2) — inner exit restored the outer lane
		ok &= expect_seed(e, 42, 2, "outer scope resumed");
	}
	PRG f;                       // (0, 1) — main lane continues where it left off
	ok &= expect_seed(f, 0, 1, "main lane resumed");
	cout << "  [lane scope nests and restores]           " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

// One parallel round: fresh pool, `tasks` tasks, each drawing a few blocks
// from two default-constructed PRGs. Returns the per-task streams in
// enqueue order. A new pool every round means worker threads differ
// between rounds — reproducibility must come from lanes, not thread ids.
static vector<vector<block>> parallel_round(int tasks) {
	reset_deterministic_seeds();
	ThreadPool pool(4);
	vector<future<vector<block>>> futs;
	for (int t = 0; t < tasks; ++t) {
		futs.push_back(pool.enqueue([]() {
			vector<block> out(12);
			PRG p;
			p.random_block(out.data(), 8);
			PRG q;
			q.random_block(out.data() + 8, 4);
			return out;
		}));
		// Main-thread draws interleaved with enqueues must not disturb
		// (or be disturbed by) the workers.
		PRG m;
		(void)m;
	}
	vector<vector<block>> res;
	for (auto& f : futs)
		res.push_back(f.get());
	return res;
}

static bool check_pool_determinism(int tasks = 12) {
	auto r1 = parallel_round(tasks);
	auto r2 = parallel_round(tasks);
	bool ok = true;
	for (int t = 0; t < tasks; ++t)
		ok &= (r1[t].size() == r2[t].size()) &&
		      cmpBlock(r1[t].data(), r2[t].data(), (int)r1[t].size());
	// Distinctness: no two tasks may share a stream.
	for (int i = 0; i < tasks && ok; ++i)
		for (int j = i + 1; j < tasks; ++j)
			if (cmpBlock(r1[i].data(), r1[j].data(), (int)r1[i].size())) {
				cout << "  [pool] tasks " << i << " and " << j << " share a stream\n";
				ok = false;
			}
	cout << "  [pool tasks reproduce across fresh pools] " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_enqueue_order_is_the_lane() {
	// The lane a task runs under is a pure function of the enqueuing
	// thread's lane (main = 0) and its enqueue ordinal — verify the k-th
	// task's first PRG seed against the derivation directly.
	reset_deterministic_seeds();
	ThreadPool pool(2);
	const int tasks = 6;
	vector<future<block>> futs;
	for (int t = 0; t < tasks; ++t)
		futs.push_back(pool.enqueue([]() { return PRG().seed(); }));
	bool ok = true;
	for (int t = 0; t < tasks; ++t) {
		uint64_t lane = detail::mix64(detail::mix64(0) ^ (uint64_t)t);
		if (lane == 0) lane = 1;
		block e = makeBlock((int64_t)lane, 0);
		block s = futs[t].get();
		ok &= cmpBlock(&e, &s, 1);
	}
	cout << "  [task lane = f(enqueue ordinal)]          " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_child_lane_derivation_deterministic() {
	// next_deterministic_child_lane() itself must reproduce: same lane, same
	// child ordinal -> same derived id, and reset rewinds it.
	reset_deterministic_seeds();
	uint64_t a0 = next_deterministic_child_lane();
	uint64_t a1 = next_deterministic_child_lane();
	reset_deterministic_seeds();
	uint64_t b0 = next_deterministic_child_lane();
	uint64_t b1 = next_deterministic_child_lane();
	bool ok = (a0 == b0) && (a1 == b1) && (a0 != a1) && (a0 != 0) && (a1 != 0);
	cout << "  [child-lane derivation reproduces]        " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_lane0_seed_owner() {
	bool ok = child_died(run_child([] {
		reset_deterministic_seeds();
		(void)next_deterministic_seed();
		thread second([] { (void)next_deterministic_seed(); });
		second.join();
	}));
	cout << "  [second lane-0 seed thread is rejected]   " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_lane0_child_owner() {
	bool ok = child_died(run_child([] {
		reset_deterministic_seeds();
		(void)next_deterministic_child_lane();
		thread second([] { (void)next_deterministic_child_lane(); });
		second.join();
	}));
	cout << "  [second lane-0 child creator is rejected] " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_lane0_thread_lifetime() {
	bool ok = child_died(run_child([] {
		reset_deterministic_seeds();
		thread first([] { (void)next_deterministic_seed(); });
		first.join();
		thread second([] { (void)next_deterministic_seed(); });
		second.join();
	}));
	cout << "  [lane-0 ownership spans thread lifetimes] " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_lane_zero_rejected() {
	bool ok = child_died(run_child([] { deterministic_lane_scope scope(0); }));
	cout << "  [lane scope rejects lane 0]               " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_reset_allows_lane0_handoff() {
	bool ok = child_succeeded(run_child([] {
		reset_deterministic_seeds();
		DeterministicSeed first_seed{};
		thread first([&] { first_seed = next_deterministic_seed(); });
		first.join();
		if (first_seed.lane != 0 || first_seed.ordinal != 0) _exit(2);

		reset_deterministic_seeds();
		DeterministicSeed second_seed{};
		thread second([&] { second_seed = next_deterministic_seed(); });
		second.join();
		if (second_seed.lane != 0 || second_seed.ordinal != 0) _exit(3);
	}));
	cout << "  [reset permits quiescent lane-0 handoff]  " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

// The seed a fresh PRG takes as `party` on the main lane at `ordinal`.
static block party_seed(uint64_t party, uint64_t ordinal) {
	return makeBlock((int64_t)deterministic_seed_high({party, 0, ordinal}), (int64_t)ordinal);
}

static bool check_party_seeds() {
	// Party 0 (none set) is the bare lane; a party offsets the lane half, the
	// same way on every run; different parties draw different seeds.
	const block none = party_seed(0, 0), p2 = party_seed(2, 0), p3 = party_seed(3, 0);
	const block bare = makeBlock(0, 0);
	bool ok = cmpBlock(&none, &bare, 1) && !cmpBlock(&p2, &bare, 1) && !cmpBlock(&p2, &p3, 1);
	for (uint64_t party : {2, 3})
		ok &= child_succeeded(run_child([party] {
			set_deterministic_party((int)party);
			reset_deterministic_seeds();
			PRG a, b;
			const block e0 = party_seed(party, 0), e1 = party_seed(party, 1);
			const block s0 = a.seed(), s1 = b.seed();
			if (!cmpBlock(&e0, &s0, 1) || !cmpBlock(&e1, &s1, 1)) _exit(2);
		}));
	cout << "  [a party offsets its seeds, reproducibly] " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_one_party_per_process() {
	// The same party again, a change before any draw as a party, and a party
	// after draws as none are fine; a change after drawing as a party aborts.
	bool ok = child_succeeded(run_child([] {
		set_deterministic_party(2);
		set_deterministic_party(2);
		set_deterministic_party(3);
		reset_deterministic_seeds();
		PRG a;
		const block e = party_seed(3, 0), s = a.seed();
		if (!cmpBlock(&e, &s, 1)) _exit(2);
	}));
	ok &= child_succeeded(run_child([] {
		reset_deterministic_seeds();
		PRG before;   // party 0's (0, 0)
		set_deterministic_party(2);
		PRG after;    // party 2's (0, 1)
		const block e = party_seed(2, 1), s = after.seed();
		if (!cmpBlock(&e, &s, 1)) _exit(2);
	}));
	ok &= child_died(run_child([] {
		set_deterministic_party(2);
		PRG a;
		set_deterministic_party(3);
	}));
	ok &= child_died(run_child([] { set_deterministic_party(0); }));
	cout << "  [one party per process]                   " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_parse_party_sets_the_party() {
	bool ok = child_succeeded(run_child([] {
		const char *argv[] = {"test", "3", nullptr};
		if (parse_party(argv, 4) != 3) _exit(2);
		reset_deterministic_seeds();
		PRG a;
		const block e = party_seed(3, 0), s = a.seed();
		if (!cmpBlock(&e, &s, 1)) _exit(3);
	}));
	cout << "  [parse_party sets the party]              " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool same_scalar(const Scalar &a, const Scalar &b) {
	unsigned char x[32], y[32];
	a.to_bin_padded(x, sizeof(x));
	b.to_bin_padded(y, sizeof(y));
	return memcmp(x, y, sizeof(x)) == 0;
}

static bool check_ec_scalar_follows_the_lane() {
	// An EC scalar comes from the drawing lane alone: a task's scalar is the one
	// its lane gives on any thread, here after another task drew on the same
	// single worker.
	ECGroup g;
	reset_deterministic_seeds();
	Scalar first, second;
	{
		ThreadPool pool(1);
		auto a = pool.enqueue([&g] { return g.rand_scalar(); });
		auto b = pool.enqueue([&g] { return g.rand_scalar(); });
		first = a.get();
		second = b.get();
	}
	uint64_t lane1 = detail::mix64(detail::mix64(0) ^ 1);
	if (lane1 == 0) lane1 = 1;
	reset_deterministic_seeds();
	Scalar want;
	{
		deterministic_lane_scope scope(lane1);
		want = g.rand_scalar();
	}
	const bool ok = same_scalar(second, want) && !same_scalar(first, second);
	cout << "  [EC scalars follow the lane, not thread]  " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

// The names this mode had before, kept for a release, still compile and agree.
static bool check_old_names() {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	static_assert(is_test_mode() == is_deterministic_mode());
	reset_test_seed_counter();
	const TestSeed t = next_test_seed();
	bool ok = t.lane == 0 && t.ordinal == 0 && current_test_seed_epoch() == current_deterministic_epoch();
	{
		test_lane_scope scope(9);
		ok &= next_test_child_lane() != 0;
	}
#pragma GCC diagnostic pop
	cout << "  [old names still compile and agree]       " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool run_correctness(bool first_draws_ok) {
	cout << "=== correctness ===\n";
	bool ok = check_first_draws(first_draws_ok);
	ok &= check_main_lane_sequence();
	ok &= check_lane_scope_nesting();
	ok &= check_lane0_seed_owner();
	ok &= check_lane0_child_owner();
	ok &= check_lane0_thread_lifetime();
	ok &= check_lane_zero_rejected();
	ok &= check_reset_allows_lane0_handoff();
	ok &= check_pool_determinism();
	ok &= check_enqueue_order_is_the_lane();
	ok &= check_child_lane_derivation_deterministic();
	ok &= check_party_seeds();
	ok &= check_one_party_per_process();
	ok &= check_parse_party_sets_the_party();
	ok &= check_ec_scalar_follows_the_lane();
	ok &= check_old_names();
	return ok;
}

int main() {
	const bool first_draws_ok = probe_first_draws();  // before any draw here
	if constexpr (!is_deterministic_mode()) {
		cout << "=== normal build: deterministic mode compiled out ===\n";
		if (!check_first_draws(first_draws_ok)) {
			cerr << "CORRECTNESS FAILURE\n";
			return 1;
		}
		return 0;
	}
	example();
	cout << "\n";
	if (!run_correctness(first_draws_ok)) {
		cerr << "CORRECTNESS FAILURE\n";
		return 1;
	}
	return 0;
}
