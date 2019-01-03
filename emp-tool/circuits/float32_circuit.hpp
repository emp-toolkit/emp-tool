inline Float32::Float32(double input, int party) {
	double abs = std::abs(input);
	double lo = pow(2, SGNFC_LEN);
	int p = 0;
	while(abs > 0. && abs < lo) {abs*=2;--p;}
	if(abs >= lo) abs = abs - lo;

	Integer expnt;
	Integer sgnfc;
	if(abs == 0.0) {
		expnt = Integer(FLOAT_LEN, 0, party);
		sgnfc = Integer(FLOAT_LEN, 0, party);
	} else {
		expnt = Integer(FLOAT_LEN, BIAS+SGNFC_LEN+p, party);
		sgnfc = Integer(FLOAT_LEN, (long long)(abs), party);
	}
	Bit sign = Bit(input>=0?0:1, party);

	Integer val(FLOAT_LEN, 0, party);
	val = val + sgnfc;
	val = val + (expnt << SGNFC_LEN);
	val[FLOAT_LEN-1] = sign;
	memcpy(value, val.bits, 32*sizeof(Bit));
}

template<>
inline string Float32::reveal<string>(int party) const {
	double lo = pow(2, SGNFC_LEN);

	Integer sgnfc = Integer(1+SGNFC_LEN, 0, party);
	for(int i = 0; i < SGNFC_LEN; i++)
		sgnfc[i] = value[i];
	double val = sgnfc.reveal<long long>(party);
	val += lo;

	Integer expnt = Integer(1+EXPNT_LEN, 0, party);
	for(int i = 0; i < EXPNT_LEN; i++)
		expnt[i] = value[i+SGNFC_LEN];
	double exp = expnt.reveal<long long>(party);
	if(exp == 0) 
		return std::to_string(0.0);
	else if(exp == ((1<<8)-1))
		return std::to_string(1.0/0.0);
	else exp -= BIAS;

	Bit sign = value[FLOAT_LEN-1];
	int sig = sign.reveal<bool>(party);
	if(!sig) sig = 1;
	else sig = -1;

	return std::to_string(sig*val*pow(2.0, exp-SGNFC_LEN)); 
}

template<>
inline double Float32::reveal<double>(int party) const {
	double lo = pow(2, SGNFC_LEN);

	Integer sgnfc = Integer(1+SGNFC_LEN, 0, party);
	for(int i = 0; i < SGNFC_LEN; i++)
		sgnfc[i] = value[i];
	double val = sgnfc.reveal<long long>(party);
	val += lo;

	Integer expnt = Integer(1+EXPNT_LEN, 0, party);
	for(int i = 0; i < EXPNT_LEN; i++)
		expnt[i] = value[i+SGNFC_LEN];
	double exp = expnt.reveal<long long>(party);
	if(exp == 0)
		return 0.0;
	else if(exp == ((1<<8)-1))
		return 1.0/0.0;
	else exp -= BIAS;

	Bit sign = value[FLOAT_LEN-1];
	int sig = sign.reveal<bool>(party);
	if(!sig) sig = 1;
	else sig = -1;

	return sig*val*pow(2.0, exp-SGNFC_LEN); 
}

inline Float32 Float32::abs() const {
	Float32 res(*this);
	res[FLOAT_LEN-1] = Bit(false, PUBLIC);
	return res;
}

inline Float32 Float32::If(const Bit& select, const Float32 & d) {
	Float32 res(*this);
	for(int i = 0; i < 32; ++i)
		res.value[i] = value[i].If(select, d.value[i]);
	return res;
}

inline Bit& Float32::operator[](int index) {
	return value[min(index, FLOAT_LEN-1)];
}

inline const Bit &Float32::operator[](int index) const {
	return value[min(index, FLOAT_LEN-1)];
}




inline Float32 Float32::operator-() const {
	Float32 res(*this);
	res[31] = !res[31];
	return res;
}


