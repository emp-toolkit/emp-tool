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
	fprintf(stderr, s, "\n");
	if(file != nullptr) {
		fprintf(stderr, "at %d, %s\n", line, file);
	}
	exit(1);
}

inline void parse_party_and_port(char ** arg, int * party, int * port) {
	*party = atoi (arg[1]);
	*port = atoi (arg[2]);
}

template <typename T>
inline T bool_to_int(const bool *data) {
    T ret {};
    for (size_t i = 0; i < sizeof(T)*8; ++i) {
        T s {data[i]};
        s <<= i;
        ret |= s;
    }
    return ret;
}

template<typename T>
inline void int_to_bool(bool * data, T input, int len) {
	for (int i = 0; i < len; ++i) {
		data[i] = (input & 1)==1;
		input >>= 1;
	}
}

inline block bool_to_block(const bool * data) {
	return makeBlock(bool_to_int<uint64_t>(data+64), bool_to_int<uint64_t>(data));
}

inline void  block_to_bool(bool * data, block b) {
	uint64_t* ptr = (uint64_t*)(&b);
	int_to_bool<uint64_t>(data, ptr[0], 64);
	int_to_bool<uint64_t>(data+64, ptr[1], 64);
}

inline bool file_exists(const std::string &name) {
	if (FILE *file = fopen(name.c_str(), "r")) {
		fclose(file);
		return true;
	}else return false;
}


