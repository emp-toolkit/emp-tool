inline time_point<high_resolution_clock> clock_start() {
	return high_resolution_clock::now();
}

inline double time_from(const time_point<high_resolution_clock>& s) {
	return std::chrono::duration_cast<std::chrono::microseconds>(high_resolution_clock::now() - s).count();
}

inline void error(const char * s, int line, const char * file) {
	fprintf(stderr, "%s\n", s);
	if(file != nullptr) {
		fprintf(stderr, "at %d, %s\n", line, file);
	}
	exit(1);
}

inline void parse_party_and_port(const char *const * arg, int * party, int * port) {
	*party = atoi (arg[1]);
	*port = atoi (arg[2]);
}

static inline uint32_t bytes_to_bits32(const void *in) {
#if defined(__AVX2__)
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
#if defined(__AVX2__)
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

inline void bools_to_bits(void *out_, const bool *bools, size_t len) {
	uint8_t *out = static_cast<uint8_t *>(out_);
	size_t full32 = len / 32;
	for (size_t i = 0; i < full32; ++i) {
		uint32_t bits = bytes_to_bits32(bools + i * 32);
		std::memcpy(out + i * 4, &bits, 4);
	}
	for (size_t i = full32 * 32; i < len; ++i) {
		uint8_t mask = (uint8_t)1 << (i % 8);
		out[i / 8] = (uint8_t)((out[i / 8] & ~mask) | (((uint8_t)bools[i]) << (i % 8)));
	}
}

inline void bits_to_bools(bool *bools, const void *in_, size_t len) {
	const uint8_t *in = static_cast<const uint8_t *>(in_);
	size_t full32 = len / 32;
	for (size_t i = 0; i < full32; ++i) {
		uint32_t bits;
		std::memcpy(&bits, in + i * 4, 4);
		bits32_to_bytes(bits, bools + i * 32);
	}
	for (size_t i = full32 * 32; i < len; ++i) {
		bools[i] = (in[i / 8] >> (i % 8)) & 1;
	}
}

template <typename T>
inline T bool_to_int(const bool *data) {
	static_assert(std::is_integral<T>::value,
	              "bool_to_int requires an integral type T");
	T ret = 0;
	bools_to_bits(&ret, data, sizeof(T) * 8);
	return ret;
}

inline block bool_to_block(const bool *data) {
	block r;
	bools_to_bits(&r, data, 128);
	return r;
}

