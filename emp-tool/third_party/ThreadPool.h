/*
https://github.com/progschj/ThreadPool

Copyright (c) 2012 Jakob Progsch, Václav Zeman

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
*/
#ifndef EMP_THREAD_POOL_H
#define EMP_THREAD_POOL_H

// Altered from the original for emp-tool: (1) in test mode
// (emp::is_test_mode()), every enqueued task runs under a deterministic
// emp::test_lane_scope whose lane is derived on the enqueuing thread, so
// pool-parallel randomness reproduces across runs regardless of which
// worker executes the task (see emp-tool/runtime/core/test_mode.h);
// (2) enqueue-on-stopped-pool reports through emp::error() instead of
// throwing, keeping the public surface exception-free
// (docs/api_conventions.md, enforced by test_no_exceptions); (3) queued tasks
// own their callable and arguments and may therefore contain move-only values.

#include "emp-tool/runtime/core/error.h"
#include "emp-tool/runtime/core/test_mode.h"

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace emp::detail {

template <bool InvokeAsLvalues, class F, class... Args>
struct thread_pool_stored_result;

template <class F, class... Args>
struct thread_pool_stored_result<true, F, Args...>
    : std::invoke_result<std::decay_t<F> &, std::decay_t<Args> &...> {};

template <class F, class... Args>
struct thread_pool_stored_result<false, F, Args...>
    : std::invoke_result<std::decay_t<F> &&, std::decay_t<Args> &&...> {};

template <class F, class... Args>
using thread_pool_stored_result_t = typename thread_pool_stored_result<
    std::is_invocable_v<std::decay_t<F> &, std::decay_t<Args> &...>, F,
    Args...>::type;

} // namespace emp::detail

class ThreadPool {
public:
	ThreadPool(size_t);
	template <class F, class... Args>
	auto enqueue(F &&f, Args &&...args)
	    -> std::future<emp::detail::thread_pool_stored_result_t<F, Args...>>;
	~ThreadPool();
	size_t size() const;

private:
	struct construction_guard {
		ThreadPool *pool;
		~construction_guard() {
			if (pool != nullptr) pool->stop_and_join();
		}
		void release() noexcept { pool = nullptr; }
	};

	void stop_and_join() noexcept;

	// need to keep track of threads so we can join them
	std::vector<std::thread> workers;
	// the task queue
	std::queue<std::function<void()>> tasks;

	// synchronization
	std::mutex queue_mutex;
	std::condition_variable condition;
	bool stop;
};

inline size_t ThreadPool::size() const { return workers.size(); }

// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads) : stop(false) {
	emp::expecting(threads > 0, "ThreadPool: worker count must be positive");
	workers.reserve(threads);
	construction_guard guard{this};
	for (size_t i = 0; i < threads; ++i)
		workers.emplace_back([this] {
			for (;;) {
				std::function<void()> task;
				{
					std::unique_lock<std::mutex> lock(this->queue_mutex);
					this->condition.wait(lock, [this] {
						return this->stop || !this->tasks.empty();
					});
					if (this->stop && this->tasks.empty()) return;
					task = std::move(this->tasks.front());
					this->tasks.pop();
				}
				task();
			}
		});
	guard.release();
}

// add new work item to the pool
template <class F, class... Args>
auto ThreadPool::enqueue(F &&f, Args &&...args)
	    -> std::future<emp::detail::thread_pool_stored_result_t<F, Args...>> {
	using return_type = emp::detail::thread_pool_stored_result_t<F, Args...>;

	auto task = std::make_shared<std::packaged_task<return_type()>>(
	    [fn = std::forward<F>(f),
	     ...bound_args = std::forward<Args>(args)]() mutable -> return_type {
			    if constexpr (std::is_invocable_v<decltype(fn) &,
			                                      decltype(bound_args) &...>)
				    return std::invoke(fn, bound_args...);
			    else
				    return std::invoke(std::move(fn), std::move(bound_args)...);
	    });

	std::future<return_type> res = task->get_future();
	// In test mode, the task's lane is derived HERE, on the enqueuing
	// thread: enqueue order is program order, so the task's randomness is
	// reproducible no matter which worker runs it, or when.
	uint64_t test_lane = 0;
	const bool lane_task = emp::is_test_mode();
	if (lane_task) test_lane = emp::next_test_child_lane();
	{
		std::unique_lock<std::mutex> lock(queue_mutex);
		if (stop)
			emp::error("ThreadPool: enqueue on stopped pool");
		if (lane_task)
			tasks.emplace([task, test_lane]() {
				emp::test_lane_scope scope(test_lane);
				(*task)();
			});
		else
			tasks.emplace([task]() { (*task)(); });
	}
	condition.notify_one();
	return res;
}

inline void ThreadPool::stop_and_join() noexcept {
	{
		std::unique_lock<std::mutex> lock(queue_mutex);
		stop = true;
	}
	condition.notify_all();
	for (std::thread &worker : workers) worker.join();
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool() { stop_and_join(); }

#endif
