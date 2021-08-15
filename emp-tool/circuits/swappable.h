#ifndef EMP_SWAPPABLE_H__
#define EMP_SWAPPABLE_H__
#include "emp-tool/circuits/bit.h"
namespace emp {
template<typename Wire>
class Bit_T;

template<typename Wire, template<typename> class T>
class Swappable { public:
	T<Wire> If(const Bit_T<Wire> & sel, const T<Wire>& rhs) const {
		return static_cast<const T<Wire>*>(this)->select(sel, rhs);
	}
};

template<typename Wire, template<typename> class T>
inline T<Wire> If(const Bit_T<Wire> & select, const T<Wire> & o1, const T<Wire> & o2) {
	T<Wire> res = o2;
	return res.If(select, o1);
}

template<typename Wire, template<typename> class T>
inline void swap(const Bit_T<Wire> & swap, T<Wire> & o1, T<Wire> & o2) {
	T<Wire> o = If(swap, o1, o2);
	o ^= o2;
	o1 ^= o;
	o2 ^= o;
}
}
#endif
