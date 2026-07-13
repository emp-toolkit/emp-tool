// core/test_mode.h — deterministic (lane, ordinal) randomness for wire-byte
// equivalence. Read example() first; the rest is verification.
//
// What's in test_mode.h:
//   is_test_mode() / set_test_mode(on)     the toggle ($EMP_TEST_MODE=1)
//   next_test_seed()                       (lane, ordinal) pair consumed by PRG()
//   test_lane_scope guard(lane)            RAII lane for the body of spawned work
//   next_test_child_lane()                 creator-side deterministic lane derivation
//   reset_test_seed_counter()              rewind every lane's ordinal, release lane 0
//
// The determinism model: seeds depend only on (which logical lane, which
// draw within it), never on cross-thread scheduling. ThreadPool::enqueue
// derives a lane per task on the enqueuing thread and installs it around
// the task body, so pool-parallel randomness reproduces across runs and
// across pool re-creations.

#include "emp-tool/emp-tool.h"

#include <iostream>
#include <vector>

using namespace emp;
using namespace std;


// ---------- example ----------

static void example() {
	cout << "=== example ===\n";
	set_test_mode(true);
	reset_test_seed_counter();

	// (1) Main thread is lane 0; ordinals count from 0.
	PRG a, b;
	cout << "  first  PRG() seed = " << a.seed() << "  (= makeBlock(0, 0))\n";
	cout << "  second PRG() seed = " << b.seed() << "  (= makeBlock(0, 1))\n";

	// (2) Spawned work runs under a creator-chosen lane.
	{
		test_lane_scope guard(7);
		PRG c;
		cout << "  lane-7 PRG() seed = " << c.seed() << "  (= makeBlock(7, 0))\n";
	}

	// (3) Reset rewinds every lane.
	reset_test_seed_counter();
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
	reset_test_seed_counter();
	PRG a, b;
	bool ok = expect_seed(a, 0, 0, "main lane") && expect_seed(b, 0, 1, "main lane");

	// The ctor id XORs into the low (ordinal) half, as for any seed.
	PRG c(nullptr, 5);
	block e = makeBlock(0, 2 ^ 5);
	block s = c.seed();
	ok &= cmpBlock(&e, &s, 1);

	// Reset rewinds the ordinal.
	reset_test_seed_counter();
	PRG d;
	ok &= expect_seed(d, 0, 0, "main lane after reset");

	cout << "  [main lane: (0,0), (0,1), id-XOR, reset]  " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool check_lane_scope_nesting() {
	reset_test_seed_counter();
	PRG a;                       // (0, 0)
	bool ok = expect_seed(a, 0, 0, "before scope");
	{
		test_lane_scope outer(42);
		PRG b, c;                // (42, 0), (42, 1)
		ok &= expect_seed(b, 42, 0, "outer scope") && expect_seed(c, 42, 1, "outer scope");
		{
			test_lane_scope inner(43);
			PRG d;               // (43, 0)
			ok &= expect_seed(d, 43, 0, "inner scope");
		}
		PRG e;                   // (42, 2) — inner exit restored the outer lane
		ok &= expect_seed(e, 42, 2, "outer scope resumed");
	}
	PRG f;                       // (0, 1) — main lane continues where it left off
	ok &= expect_seed(f, 0, 1, "main lane resumed");
	cout << "  [test_lane_scope nests and restores]      " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

// One parallel round: fresh pool, `tasks` tasks, each drawing a few blocks
// from two default-constructed PRGs. Returns the per-task streams in
// enqueue order. A new pool every round means worker threads differ
// between rounds — reproducibility must come from lanes, not thread ids.
static vector<vector<block>> parallel_round(int tasks) {
	reset_test_seed_counter();
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
	reset_test_seed_counter();
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
	// next_test_child_lane() itself must reproduce: same lane, same
	// child ordinal -> same derived id, and reset rewinds it.
	reset_test_seed_counter();
	uint64_t a0 = next_test_child_lane();
	uint64_t a1 = next_test_child_lane();
	reset_test_seed_counter();
	uint64_t b0 = next_test_child_lane();
	uint64_t b1 = next_test_child_lane();
	bool ok = (a0 == b0) && (a1 == b1) && (a0 != a1) && (a0 != 0) && (a1 != 0);
	cout << "  [child-lane derivation reproduces]        " << (ok ? "OK" : "FAIL") << "\n";
	return ok;
}

static bool run_correctness() {
	cout << "=== correctness ===\n";
	bool ok = true;
	ok &= check_main_lane_sequence();
	ok &= check_lane_scope_nesting();
	ok &= check_pool_determinism();
	ok &= check_enqueue_order_is_the_lane();
	ok &= check_child_lane_derivation_deterministic();
	return ok;
}

int main(int /*argc*/, char ** /*argv*/) {
	set_test_mode(true);
	example();
	cout << "\n";
	if (!run_correctness()) {
		cerr << "CORRECTNESS FAILURE\n";
		return 1;
	}
	return 0;
}
