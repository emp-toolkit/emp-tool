template<class... Ts>
void run_function(void *function, const Ts&... args) {	
	reinterpret_cast<void(*)(Ts...)>(function)(args...);
}

template<typename T>
void inline delete_array_null(T * ptr){
	if(ptr != nullptr) {
		delete[] ptr;
		ptr = nullptr;
	}
}

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

inline bool file_exists(const std::string &name) {
	if (FILE *file = fopen(name.c_str(), "r")) {
		fclose(file);
		return true;
	}else return false;
}


