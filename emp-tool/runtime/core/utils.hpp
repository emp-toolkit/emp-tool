#ifndef EMP_UTILS_HPP__
#define EMP_UTILS_HPP__

// Implementation half of utils.h, included by it after the namespace closes
// (same pattern as block.hpp / aes.hpp / f2k.hpp).

namespace emp {

inline std::chrono::steady_clock::time_point clock_start() {
	return std::chrono::steady_clock::now();
}

inline double time_from(const std::chrono::steady_clock::time_point& s) {
	return std::chrono::duration_cast<std::chrono::microseconds>(
	           std::chrono::steady_clock::now() - s)
	    .count();
}

// Wait for every future in `res`, then clear it — a barrier folding a batch of
// ThreadPool tasks back together. Templated so it serves future<void> and
// future<T> alike.
template <typename T>
inline void joinNclean(std::vector<std::future<T>>& res) {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
	std::exception_ptr failure;
	for (auto& v : res) {
		try {
			v.get();
		} catch (...) {
			if (!failure) failure = std::current_exception();
		}
	}
	res.clear();
	if (failure) std::rethrow_exception(failure);
#else
	for (auto& v : res) v.get();
	res.clear();
#endif
}
// joinNclean that OR-reduces the bool results (e.g. "did any task flag a cheat?").
inline bool joinNcleanCheat(std::vector<std::future<bool>>& res) {
	bool cheat = false;
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
	std::exception_ptr failure;
	for (auto& v : res) {
		try {
			if (v.get()) cheat = true;
		} catch (...) {
			if (!failure) failure = std::current_exception();
		}
	}
	res.clear();
	if (failure) std::rethrow_exception(failure);
#else
	for (auto& v : res)
		if (v.get()) cheat = true;
	res.clear();
#endif
	return cheat;
}

// Party is per-process (argv[1]); port and peer IP are session config shared by
// both parties, read from the environment so a two-machine run sets EMP_PORT /
// EMP_PEER_IP once per host with no source change. One consequence: two runs on
// the same host share EMP_PORT, so don't launch them concurrently.
namespace detail {
inline int parse_bounded_int(const char *text, int lower, int upper,
                             const char *message) {
	expecting(text != nullptr && text[0] != '\0', message);
	int value = 0;
	const char *end = text + std::strlen(text);
	auto parsed = std::from_chars(text, end, value);
	expecting(parsed.ec == std::errc{} && parsed.ptr == end &&
	              value >= lower && value <= upper,
	          message);
	return value;
}
}  // namespace detail

inline int parse_party(const char *const * arg, int max_party) {
	const char *text = arg ? arg[1] : nullptr;
	return detail::parse_bounded_int(
	    text, ALICE, max_party,
	    "parse_party: argv[1] (party) must be an integer in [1, max_party] "
	    "(default max is BOB=2; multi-party callers pass nP)");
}
inline int peer_port() {
	const char * e = std::getenv("EMP_PORT");
	return (e && e[0])
	           ? detail::parse_bounded_int(
	                 e, 1, 65535,
	                 "peer_port: EMP_PORT must be an integer in [1, 65535]")
	           : 12345;
}
inline const char * peer_ip() {
	const char * e = std::getenv("EMP_PEER_IP");
	return (e && e[0]) ? e : "127.0.0.1";
}

static inline uint32_t bytes_to_bits32(const void *in) {
#if EMP_HAS_AVX2
    __m256i v = _mm256_loadu_si256((const __m256i *)in);
    v = _mm256_cmpeq_epi8(v, _mm256_setzero_si256());
    return ~(uint32_t)_mm256_movemask_epi8(v);
#else
    __m128i lo = _mm_loadu_si128((const __m128i *)in);
    __m128i hi = _mm_loadu_si128(((const __m128i *)in) + 1);
    const __m128i z = _mm_setzero_si128();
    lo = _mm_cmpeq_epi8(lo, z);
    hi = _mm_cmpeq_epi8(hi, z);
    uint32_t r =  (uint32_t)(uint16_t)_mm_movemask_epi8(lo)
               | ((uint32_t)(uint16_t)_mm_movemask_epi8(hi) << 16);
    return ~r;
#endif
}

static inline void bits32_to_bytes(uint32_t bits, void *out) {
#if EMP_HAS_AVX2
    __m256i v = _mm256_set1_epi32((int)bits);
    const __m256i shuf = _mm256_setr_epi8(
        0,0,0,0,0,0,0,0, 1,1,1,1,1,1,1,1,
        2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3);
    const __m256i mask = _mm256_setr_epi8(
        1,2,4,8,16,32,64,(char)128, 1,2,4,8,16,32,64,(char)128,
        1,2,4,8,16,32,64,(char)128, 1,2,4,8,16,32,64,(char)128);
    v = _mm256_shuffle_epi8(v, shuf);
    v = _mm256_and_si256(v, mask);
    // After mask-AND each byte is 0 or one of {1,2,4,...,128}; min(.,1)
    // collapses both to 0/1 in a single op.
    v = _mm256_min_epu8(v, _mm256_set1_epi8(1));
    _mm256_storeu_si256((__m256i *)out, v);
#else
    __m128i v = _mm_cvtsi32_si128((int)bits);
    const __m128i shuf_lo = _mm_setr_epi8(0,0,0,0,0,0,0,0, 1,1,1,1,1,1,1,1);
    const __m128i shuf_hi = _mm_setr_epi8(2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3);
    const __m128i mask    = _mm_setr_epi8(1,2,4,8,16,32,64,(char)128,
                                          1,2,4,8,16,32,64,(char)128);
    __m128i lo = _mm_shuffle_epi8(v, shuf_lo);
    __m128i hi = _mm_shuffle_epi8(v, shuf_hi);
    lo = _mm_and_si128(lo, mask);
    hi = _mm_and_si128(hi, mask);
    const __m128i one = _mm_set1_epi8(1);
    lo = _mm_min_epu8(lo, one);
    hi = _mm_min_epu8(hi, one);
    _mm_storeu_si128((__m128i *)out,       lo);
    _mm_storeu_si128((__m128i *)out + 1,   hi);
#endif
}

namespace detail {

template <typename T>
inline void bools_to_bits_impl(void *out_, const T *bools, int64_t len) {
	expecting(len >= 0, "bools_to_bits: negative bit count");
	uint8_t *out = static_cast<uint8_t *>(out_);
	int64_t full32 = len / 32;
	for (int64_t i = 0; i < full32; ++i) {
		uint32_t bits = bytes_to_bits32(bools + i * 32);
		std::memcpy(out + i * 4, &bits, 4);
	}
	for (int64_t i = full32 * 32; i < len; ++i) {
		uint8_t mask = (uint8_t)1 << (i % 8);
		uint8_t bit = static_cast<uint8_t>(bools[i] != 0);
		out[i / 8] = (uint8_t)((out[i / 8] & ~mask) | (bit << (i % 8)));
	}
}

template <typename T>
inline void bits_to_bools_impl(T *bools, const void *in_, int64_t len) {
	expecting(len >= 0, "bits_to_bools: negative bit count");
	const uint8_t *in = static_cast<const uint8_t *>(in_);
	int64_t full32 = len / 32;
	for (int64_t i = 0; i < full32; ++i) {
		uint32_t bits;
		std::memcpy(&bits, in + i * 4, 4);
		bits32_to_bytes(bits, bools + i * 32);
	}
	for (int64_t i = full32 * 32; i < len; ++i) {
		bools[i] = (in[i / 8] >> (i % 8)) & 1;
	}
}

}  // namespace detail

inline void bools_to_bits(void *out, const bool *bools, int64_t len) {
	detail::bools_to_bits_impl(out, bools, len);
}

template <typename T>
requires std::is_same_v<T, uint8_t>
inline void bools_to_bits(void *out, const T *bools, int64_t len) {
	detail::bools_to_bits_impl(out, bools, len);
}

inline void bits_to_bools(bool *bools, const void *in, int64_t len) {
	detail::bits_to_bools_impl(bools, in, len);
}

template <typename T>
requires std::is_same_v<T, uint8_t>
inline void bits_to_bools(T *bools, const void *in, int64_t len) {
	detail::bits_to_bools_impl(bools, in, len);
}

template <typename T>
inline T bool_to_int(const bool *data) {
	static_assert(std::is_integral<T>::value,
	              "bool_to_int requires an integral type T");
	static_assert(!std::is_same<typename std::remove_cv<T>::type, bool>::value,
	              "bool_to_int does not support bool");
	T ret = 0;
	bools_to_bits(&ret, data, sizeof(T) * 8);
	return ret;
}

inline block bool_to_block(const bool *data) {
	block r;
	bools_to_bits(&r, data, 128);
	return r;
}

}  // namespace emp
#endif  // EMP_UTILS_HPP__
