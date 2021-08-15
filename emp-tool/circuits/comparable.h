#ifndef EMP_COMPARABLE_H__
#define EMP_COMPARABLE_H__

namespace emp {
template<typename Wire, template<typename> class T>
class Comparable { public:
	Bit_T<Wire> operator>=(const T<Wire>&rhs) const {
		return static_cast<const T<Wire>*>(this)->geq(rhs);
	}
	Bit_T<Wire> operator<(const T<Wire>& rhs) const {
		return !( (*static_cast<const T<Wire>*>(this))>= rhs );
	}

	Bit_T<Wire> operator<=(const T<Wire>& rhs) const {
		return rhs >= *static_cast<const T<Wire>*>(this);
	}

	Bit_T<Wire> operator>(const T<Wire>& rhs) const {
		return !(rhs >= *static_cast<const T<Wire>*>(this));
	}
	
	Bit_T<Wire> operator==(const T<Wire>& rhs) const {
		return static_cast<const T<Wire>*>(this)->equal(rhs);
	}
	Bit_T<Wire> operator!=(const T<Wire>& rhs) const {
		return !(*static_cast<const T<Wire>*>(this) == rhs);
	}
};
}
#endif
