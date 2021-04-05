#ifndef EMP_SWAPPABLE_H__
#define EMP_SWAPPABLE_H__
#include "emp-tool/circuits/bit.h"
namespace emp {
class Bit;
template<typename T>
class Swappable { public:
	T If(const Bit & sel, const T& rhs) const {
		return static_cast<const T*>(this)->select(sel, rhs);
	}
};
template<typename T>
inline T If(const Bit & select, const T & o1, const T & o2) {
	T res = o2;
	return res.If(select, o1);
}
template<typename T>
inline void swap(const Bit & swap, T & o1, T & o2) {
	T o = If(swap, o1, o2);
	o ^= o2;
	o1 ^= o;
	o2 ^= o;
}
}
#endif
