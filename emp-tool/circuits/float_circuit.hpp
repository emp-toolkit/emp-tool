template<typename T>
inline Float<T>::Float(int value_length, int expnt_length, double input, int party) {
   double abs = input > 0 ? input:-1*input;
   double lo = pow(2, value_length-2);
   double up = pow(2, value_length-1);
   int p = 0;
   while(abs < lo) {abs*=2;--p;}
   while(abs >= up) {abs/=2;++p;}
   expnt = Integer<T>(expnt_length, p, party);
   value = Integer<T>(value_length, (long long)(abs*(input > 0 ? 1: -1)), party);
}

template<typename T>
inline Float<T> Float<T>::If(const Bit<T> & select, const Float& d) {
	Float res(*this);
   res.value = value.If(select, d.value);
   res.expnt = expnt.If(select, d.expnt);
   return res;
}

template<typename T>
inline double Float<T>::reveal(int party) const {
   double val = value.reveal(party);
   double exp = expnt.reveal(party);
   return val*pow(2.0, exp); 
}

template<typename T>
inline string Float<T>::detail(int party) const {
   string res = string(reveal(party));
   res += (" v: " + string(value.reveal(party)));
   res += (" p: " + string(expnt.reveal(party)));
   return res;
}

template<typename T>
inline int Float<T>::size() const {
   return value.size()+expnt.size();
}

template<typename T>
inline Float<T> Float<T>::abs() const {
   Float res(*this);
   res.value = value.abs();
   return res;
}

template<typename T>
inline void Float<T>::normalize(int value_length, int to_add_to_expnt) {
   Integer<T> value_before_normalize = value;
   Integer<T> bits_to_shift(value_before_normalize.size(), 0, PUBLIC);
   for(int i = bits_to_shift.size()-1; i>0; --i)
      bits_to_shift[i] = value_before_normalize[i] ^ value_before_normalize[i-1];
   bits_to_shift[0] = true;

   Integer<T> shift_amount = bits_to_shift.leading_zeros();
   value_before_normalize = value_before_normalize << shift_amount;
	Integer<T> shift_amount_extended = shift_amount;
	shift_amount_extended.resize(expnt.size());
   expnt = expnt - shift_amount_extended;
//	std::cout << expnt.size()<<" "<<shift_amount.size()<<endl;

	value = value_before_normalize;
	value = value >> (value_before_normalize.size() - value_length);
	value.resize(value_length);
   expnt = expnt + Integer<T>(expnt.size(), (value_before_normalize.size() + to_add_to_expnt - value_length), PUBLIC);
}

template<typename T>
inline Float<T> Float<T>::operator+(const Float& rhs) const{
	Float res(*this);
   Integer<T> diff = expnt - rhs.expnt;
   Integer<T> diff_abs = diff.abs();
   Bit<T> to_swap = diff[diff.size()-1];
   res.expnt = expnt.If(!to_swap, rhs.expnt);

   Integer<T> v1 = value, v2 = rhs.value;
   swap(to_swap, v1, v2);

   v1.resize(2*value.size(), true);
   v1 = v1 << diff_abs;
   v2.resize(2*value.size(), true);
   res.value = v1 + v2;
   res.normalize(rhs.value.size(), 0);
   return res;
}

template<typename T>
inline Float<T> Float<T>::operator-(const Float& rhs) const {
	return (*this) + (-rhs);
}

template<typename T>
inline Float<T> Float<T>::operator-() const {
   Float res = *this;
   res.value = -res.value;
   return res;
}

template<typename T>
inline Float<T> Float<T>::operator*(const Float& rhs) const {
	Float res(*this);
	Integer<T> v1 = value;
	Integer<T> v2 = rhs.value;
	v1.resize(value.size()*2);
	v2.resize(value.size()*2);
	v1 = v1*v2;
   Bit<T> extra_move = v1[v1.size()-1] == v1[v1.size()-2];
   v1 = v1.If(extra_move, v1 << 1);

	res.value = v1 >> (v1.size() - rhs.value.size());
	res.value.resize(rhs.value.size());
   res.expnt = res.expnt + rhs.expnt;

   res.expnt = res.expnt.If(extra_move, res.expnt - Integer<T>(res.expnt.size(), 1));
   res.expnt = res.expnt +  Integer<T>(res.expnt.size(), (v1.size()-rhs.value.size()), PUBLIC);
   return res;
}

template<typename T>
inline Integer<T> divide_frac(const Integer<T>& lhs, const Integer<T>& rhs) {
   Integer<T> i1 = lhs, i2 = rhs;
   i1.resize(rhs.size()*2+1, false);
   i2.resize(rhs.size()*2+1, false);
   i1=i1<<rhs.size();
   Integer<T> res(2*rhs.size(), 0, 0);
   for(int i = 0; i <= rhs.size(); ++i) {
		Integer<T> tmp = i1 >> (rhs.size()-i);
//		tmp.resize(i1.size() - rhs.size() +i);
//		std::cout <<"...\n"<<endl;
      Integer<T> diff = tmp - i2;
//		std::cout <<"...!\n"<<endl;
      res[rhs.size()-i] = !diff[diff.size()-1];
      tmp = tmp.If(!diff[diff.size()-1], diff);
      for(int j = 0; j < tmp.size(); ++j)
         i1[rhs.size()+j-i] = tmp[j];
   }
   return res;
}


//to optimize
template<typename T>
inline Float<T> Float<T>::operator/(const Float& rhs) const {
	Float res(*this);
   Bit<T> sign = rhs.value[rhs.value.size()-1] != value[value.size()-1];
   Integer<T> v_tmp = value.abs();
   Integer<T> rhs_v_tmp = rhs.value.abs();
   res.value = divide_frac(v_tmp, rhs_v_tmp);
   res.value = res.value.If(sign, -value);
   res.expnt = res.expnt - rhs.expnt;
   res.normalize(rhs.value.size(), -1*rhs.value.size());
   return res;
}

template<typename T>
inline Float<T> Float<T>::operator|(const Float& rhs) const{
	Float res(*this);
   res.expnt = expnt | rhs.expnt;
   res.value = value | rhs.value;
   return res;
}

template<typename T>
inline Float<T> Float<T>::operator^(const Float& rhs) const{
	Float res(*this);
   res.expnt = expnt ^ rhs.expnt;
   res.value = value ^ rhs.value;
   return res;
}

template<typename T>
inline Float<T> Float<T>::operator&(const Float& rhs) const{
	Float res(*this);
   res.expnt = expnt & rhs.expnt;
   res.value = value & rhs.value;
   return res;
}

template<typename T>
inline Bit<T> Float<T>::greater(const Float& rhs) const {
   Float tmp = (*this) - rhs;
   return !tmp.value[tmp.value.size()-1];
}

template<typename T>
inline Bit<T> Float<T>::equal(const Float& rhs) const {
	return false;
}
