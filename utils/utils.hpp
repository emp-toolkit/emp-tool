static const char* hex_char_to_bin(char c)
{
	switch(toupper(c))
	{
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
inline void parse_party_and_port(char ** arg, int * party, int * port) {
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

inline uint64_t timeStamp() {
	struct timespec t;
	clock_gettime(CLOCK_REALTIME,&t);
	return (t.tv_sec*1000*1000+t.tv_nsec/1000);
}
inline double wallClock() {
	struct timespec t;
	clock_gettime(CLOCK_REALTIME,&t);
	return t.tv_sec+1e-9*t.tv_nsec;
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
