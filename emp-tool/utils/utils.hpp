template<class... Ts>
void run_function(void *function, const Ts&... args) {	
	reinterpret_cast<void(*)(Ts...)>(function)(args...);
}

template <typename T>
std::string m128i_to_string(const __m128i var) {
	std::stringstream sstr;
	const T* values = (const T*) &var;
	for (unsigned int i = 0; i < sizeof(__m128i) / sizeof(T); i++) {
		sstr <<"0x"<<std::hex<< values[i] << " ";
	}
	return sstr.str();
}

inline time_point<high_resolution_clock> clock_start() { 
	return high_resolution_clock::now();
}

inline double time_from(const time_point<high_resolution_clock>& s) {
	return std::chrono::duration_cast<std::chrono::microseconds>(high_resolution_clock::now() - s).count();
}

inline string bin_to_dec(const string& bin2) {
	if(bin2[0] == '0')
		return change_base(bin2, 2, 10);
	string bin = bin2;
	bool flip = false;
	for(int i = bin.size()-1; i>=0; --i) {
		if(flip)
			bin[i] = (bin[i] == '1' ? '0': '1');
		if(bin[i] == '1')
			flip = true;
	}
	return "-"+change_base(bin, 2, 10);
}

inline string dec_to_bin(const string& dec) {
	string bin = change_base(dec, 10, 2);
	if(dec[0] != '-')
		return '0' + bin;
	bin[0] = '1';
	bool flip = false;
	for(int i = bin.size()-1; i>=1; --i) {
		if(flip)
			bin[i] = (bin[i] == '1' ? '0': '1');
		if(bin[i] == '1')
			flip = true;
	}
	return bin;
}

inline string change_base(string str, int old_base, int new_base) {
	mpz_t tmp;
	mpz_init_set_str (tmp, str.c_str(), old_base);
	char * b = new char[mpz_sizeinbase(tmp, new_base) + 2];
	mpz_get_str(b, new_base, tmp);
	mpz_clear(tmp);
	string res(b);
	delete[]b;
	return res;
}

inline void error(const char * s, int line, const char * file) {
	fprintf(stderr, s, "\n");
	if(file != nullptr) {
		fprintf(stderr, "at %d, %s\n", line, file);
	}
	exit(1);
}

inline const char* hex_char_to_bin(char c) {
	switch(toupper(c)) {
		case '0': return "0000";
		case '1': return "0001";
		case '2': return "0010";
		case '3': return "0011";
		case '4': return "0100";
		case '5': return "0101";
		case '6': return "0110";
		case '7': return "0111";
		case '8': return "1000";
		case '9': return "1001";
		case 'A': return "1010";
		case 'B': return "1011";
		case 'C': return "1100";
		case 'D': return "1101";
		case 'E': return "1110";
		case 'F': return "1111";
		default: return "0";
	}
}

inline std::string hex_to_binary(std::string hex) {
	std::string bin;
	for(unsigned i = 0; i != hex.length(); ++i)
		bin += hex_char_to_bin(hex[i]);
	return bin;
}
inline void parse_party_and_port(char ** arg, int argc, int * party, int * port) {
	if (argc == 1)
		error("ERROR: argc = 1, need two argsm party ID {1,2} and port.");
	*party = atoi (arg[1]);
	*port = atoi (arg[2]);
}

inline std::string Party(int p) {
	if (p == ALICE)	
		return "ALICE";
	else if (p == BOB)
		return "BOB";
	else return "PUBLIC";
}

template<typename t>
t bool_to_int(const bool * data, size_t len) {
	if (len != 0) len = (len > sizeof(t)*8 ? sizeof(t)*8 : len);
	else len = sizeof(t)*8;
	t res = 0;
	for(size_t i = 0; i < len-1; ++i) {
		if(data[i])
			res |= (1LL<<i);
	}
	if(data[len-1]) return -1*res;
	else return res;
}

inline uint64_t bool_to64(const bool * data) {
	uint64_t res = 0;
	for(int i = 0; i < 64; ++i) {
		if(data[i])
			res |= (1ULL<<i);
	}
	return res;
}
inline block bool_to128(const bool * data) {
	return makeBlock(bool_to64(data+64), bool_to64(data));
}

inline void int64_to_bool(bool * data, uint64_t input, int length) {
	for (int i = 0; i < length; ++i) {
		data[i] = (input & 1)==1;
		input >>= 1;
	}
}
