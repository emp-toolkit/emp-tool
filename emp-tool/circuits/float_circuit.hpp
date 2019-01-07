inline Float::Float(int value_length, int expnt_length, double input, int party) {
   double abs = std::abs(input);
   double lo = pow(2, value_length-2);
   double up = pow(2, value_length-1);
   int p = 0;
   while(abs > 0. && abs < lo) {abs*=2;--p;}
   while(abs >= up) {abs/=2;++p;}
   expnt = Integer(expnt_length, p, party);
   value = Integer(value_length, (long long)(abs*(input > 0 ? 1: -1)), party);
}

inline Float Float::If(const Bit & select, const Float& d) {
	Float res(*this);
   res.value = value.If(select, d.value);
   res.expnt = expnt.If(select, d.expnt);
   return res;
}

template<>
inline string Float::reveal<string>(int party) const {
   double val = value.reveal<int64_t>(party);
   double exp = expnt.reveal<int64_t>(party);
   return std::to_string(val*pow(2.0, exp)); 
}

template<>
inline double Float::reveal<double>(int party) const {
   double val = value.reveal<int64_t>(party);
   double exp = expnt.reveal<int64_t>(party);
   return val*pow(2.0, exp); 
}

inline string Float::detail(int party) const {
   string res = reveal<string>(party);
   res += (" v: " + value.reveal<string>(party));
   res += (" p: " + expnt.reveal<string>(party));
   return res;
}

inline int Float::size() const {
   return value.size()+expnt.size();
}

inline Float Float::abs() const {
   Float res(*this);
   res.value = value.abs();
   return res;
}

inline void Float::normalize(int value_length, int to_add_to_expnt) {
   Integer value_before_normalize = value;
   Integer bits_to_shift(value_before_normalize.size(), 0, PUBLIC);
   for(int i = bits_to_shift.size()-1; i>0; --i)
      bits_to_shift[i] = value_before_normalize[i] ^ value_before_normalize[i-1];
   bits_to_shift[0] = true;

   Integer shift_amount = bits_to_shift.leading_zeros();
   value_before_normalize = value_before_normalize << shift_amount;
	Integer shift_amount_extended = shift_amount;
	shift_amount_extended.resize(expnt.size());
   expnt = expnt - shift_amount_extended;
//	std::cout << expnt.size()<<" "<<shift_amount.size()<<endl;

	value = value_before_normalize;
	value = value >> (value_before_normalize.size() - value_length);
	value.resize(value_length);
   expnt = expnt + Integer(expnt.size(), (value_before_normalize.size() + to_add_to_expnt - value_length), PUBLIC);
}

inline Float Float::operator+(const Float& rhs) const{
	Float res(*this);
   Integer diff = expnt - rhs.expnt;
   Integer diff_abs = diff.abs();
   Bit to_swap = diff[diff.size()-1];
   res.expnt = expnt.If(!to_swap, rhs.expnt);

   Integer v1 = value, v2 = rhs.value;
   swap(to_swap, v1, v2);

   v1.resize(2*value.size(), true);
   v1 = v1 << diff_abs;
   v2.resize(2*value.size(), true);
   res.value = v1 + v2;
   res.normalize(rhs.value.size(), 0);
   return res;
}

inline Float Float::operator-(const Float& rhs) const {
	return (*this) + (-rhs);
}

inline Float Float::operator-() const {
   Float res = *this;
   res.value = -res.value;
   return res;
}

inline Float Float::operator*(const Float& rhs) const {
	Float res(*this);
	Integer v1 = value;
	Integer v2 = rhs.value;
	v1.resize(value.size()*2);
	v2.resize(value.size()*2);
	v1 = v1*v2;
   Bit extra_move = v1[v1.size()-1] == v1[v1.size()-2];
   v1 = v1.If(extra_move, v1 << 1);

	res.value = v1 >> (v1.size() - rhs.value.size());
	res.value.resize(rhs.value.size());
   res.expnt = res.expnt + rhs.expnt;

   res.expnt = res.expnt.If(extra_move, res.expnt - Integer(res.expnt.size(), 1));
   res.expnt = res.expnt +  Integer(res.expnt.size(), (v1.size()-rhs.value.size()), PUBLIC);
   return res;
}

inline Integer divide_frac(const Integer& lhs, const Integer& rhs) {
   Integer i1 = lhs, i2 = rhs;
   i1.resize(rhs.size()*2+1, false);
   i2.resize(rhs.size()*2+1, false);
   i1=i1<<rhs.size();
   Integer res(2*rhs.size(), 0, 0);
   for(int i = 0; i <= rhs.size(); ++i) {
		Integer tmp = i1 >> (rhs.size()-i);
//		tmp.resize(i1.size() - rhs.size() +i);
//		std::cout <<"...\n"<<endl;
      Integer diff = tmp - i2;
//		std::cout <<"...!\n"<<endl;
      res[rhs.size()-i] = !diff[diff.size()-1];
      tmp = tmp.If(!diff[diff.size()-1], diff);
      for(int j = 0; j < tmp.size(); ++j)
         i1[rhs.size()+j-i] = tmp[j];
   }
   return res;
}


//to optimize
inline Float Float::operator/(const Float& rhs) const {
	Float res(*this);
   Bit sign = rhs.value[rhs.value.size()-1] != value[value.size()-1];
   Integer v_tmp = value.abs();
   Integer rhs_v_tmp = rhs.value.abs();
   res.value = divide_frac(v_tmp, rhs_v_tmp);
   res.value = res.value.If(sign, -value);
   res.expnt = res.expnt - rhs.expnt;
   res.normalize(rhs.value.size(), -1*rhs.value.size());
   return res;
}

inline Float Float::operator|(const Float& rhs) const{
	Float res(*this);
   res.expnt = expnt | rhs.expnt;
   res.value = value | rhs.value;
   return res;
}

inline Float Float::operator^(const Float& rhs) const{
	Float res(*this);
   res.expnt = expnt ^ rhs.expnt;
   res.value = value ^ rhs.value;
   return res;
}

inline Float Float::operator&(const Float& rhs) const{
	Float res(*this);
   res.expnt = expnt & rhs.expnt;
   res.value = value & rhs.value;
   return res;
}

inline Bit Float::greater(const Float& rhs) const {
   Float tmp = (*this) - rhs;
   return !tmp.value[tmp.value.size()-1];
}
inline Bit Float::equal(const Float& rhs) const {
	return false;
}
