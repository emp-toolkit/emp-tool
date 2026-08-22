#ifndef EMP_ERROR_H__
#define EMP_ERROR_H__

// Lightweight fatal-error primitive shared by the lowest-level runtime
// headers. Kept separate from utils.h so block.h can validate arguments
// without creating a block.h <-> utils.h include cycle.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

namespace emp {

// Defaults capture the caller's source location via __builtin_LINE /
// __builtin_FILE — both evaluate at the call site, so an unadorned
// `error("msg")` records where it fired.
#if defined(__GNUC__) || defined(__clang__)
[[noreturn, gnu::cold, gnu::noinline]]
#else
[[noreturn]]
#endif
inline void error(const char *s,
				  int line = __builtin_LINE(),
				  const char *file = __builtin_FILE()) {
	std::fprintf(stderr, "%s at %s:%d\n", s, file, line);
	std::fflush(stderr);
	// _Exit, not exit(): error() can fire from a worker thread while sibling
	// workers still own heap state. Running destructors/atexit handlers in that
	// situation races their live work; terminate the process immediately.
	std::_Exit(1);
}

// Enforce a condition that should be true on the normal path. The condition is
// evaluated exactly once; GCC/Clang receive a true-likely hint for code layout
// and initial prediction. Caller location defaults are captured here and passed
// through to error(), so diagnostics point at the failed expectation rather
// than this helper.
//
//   expecting(len >= 0, "negative length");
//
// Do not use this for ordinary data-dependent branches with no dominant result.
#if defined(__GNUC__) || defined(__clang__)
[[gnu::always_inline]]
#endif
inline void expecting(bool condition, const char *message,
					  int line = __builtin_LINE(),
					  const char *file = __builtin_FILE()) {
#if defined(__has_builtin)
#if __has_builtin(__builtin_expect)
	if (__builtin_expect(condition, true)) return;
#else
	if (condition) return;
#endif
#elif defined(__GNUC__)
	if (__builtin_expect(condition, true)) return;
#else
	if (condition) return;
#endif
	error(message, line, file);
}

// Lazy-message overload for diagnostics that need formatting. The callable is
// invoked only on failure, so filenames, wire ids, OpenSSL errors, etc. impose
// no construction or allocation cost on the expected path.
//
//   expecting(fd >= 0, [&] { return "open failed: " + path; });
template <typename MessageFactory>
	requires requires(MessageFactory &&factory) {
		std::forward<MessageFactory>(factory)();
	}
#if defined(__GNUC__) || defined(__clang__)
[[gnu::always_inline]]
#endif
inline void expecting(bool condition, MessageFactory &&make_message,
					  int line = __builtin_LINE(),
					  const char *file = __builtin_FILE()) {
#if defined(__has_builtin)
#if __has_builtin(__builtin_expect)
	if (__builtin_expect(condition, true)) return;
#else
	if (condition) return;
#endif
#elif defined(__GNUC__)
	if (__builtin_expect(condition, true)) return;
#else
	if (condition) return;
#endif
	std::string message(std::forward<MessageFactory>(make_message)());
	error(message.c_str(), line, file);
}

}  // namespace emp

#endif  // EMP_ERROR_H__
