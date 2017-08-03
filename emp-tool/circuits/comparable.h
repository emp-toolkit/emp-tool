#ifndef COMPARABLE_H__
#define COMPARABLE_H__

template<typename T1, template<typename> class T>
class Comparable { public:
	Bit<T1> operator>=(const T<T1>&rhs) const {
		return static_cast<const T<T1>*>(this)->geq(rhs);
	}
	Bit<T1> operator<(const T<T1>& rhs) const {
		return !( (*static_cast<const T<T1>*>(this))>= rhs );
	}

	Bit<T1> operator<=(const T<T1>& rhs) const {
		return rhs >= *static_cast<const T<T1>*>(this);
	}

	Bit<T1> operator>(const T<T1>& rhs) const {
		return !(rhs >= *static_cast<const T<T1>*>(this));
	}
	
	Bit<T1> operator==(const T<T1>& rhs) const {
		return static_cast<const T<T1>*>(this)->equal(rhs);
	}
	Bit<T1> operator!=(const T<T1>& rhs) const {
		return !(*static_cast<const T<T1>*>(this) == rhs);
	}
};
#endif
