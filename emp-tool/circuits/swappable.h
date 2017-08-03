#ifndef SWAPPABLE_H__
#define SWAPPABLE_H__
#include "emp-tool/circuits/bit.h"

template<typename T>
class Bit;

template<typename T1, template<typename> class T>
class Swappable { public:
	T<T1> If(const Bit<T1> & sel, const T<T1>& rhs) const {
		return static_cast<const T<T1>*>(this)->select(sel, rhs);
	}
};
template<typename T1, template<typename> class T>
inline T<T1> If(const Bit<T1> & select, const T<T1> & o1, const T<T1> & o2) {
	T<T1> res = o2;
	return res.If(select, o1);
}
template<typename T1, template<typename> class T>
inline void swap(const Bit<T1> & swap, T<T1> & o1, T<T1> & o2) {
	T<T1> o = If(swap, o1, o2);
	o = o ^ o2;
	o1 = o ^ o1;
	o2 = o ^ o2;
}

#endif
