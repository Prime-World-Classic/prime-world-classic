#include <stddef.h>
namespace ring {
    template <class T, class C, class R> struct Ring {
        struct Part { int prev, next; };
        static void part(T* obj) { C::part(obj); }
    };
}
#define seDECLARE_RING_CLASS_BASE(cls, field, ringCls, refPolicy) \
template <typename A> struct ring ## _ ## ringCls ## _; \
typedef ::ring::Ring<cls, ring ## _ ## ringCls ## _<int>, refPolicy > ringCls; \
template <typename A> struct ring ## _ ## ringCls ## _ { \
	static typename ringCls::Part& part(cls * obj) { return obj->field; } \
	static cls * obj(typename ringCls::Part const * _part_) { \
	typename ringCls::Part cls::* field##_offset = &cls::field;\
	return reinterpret_cast<cls*>(reinterpret_cast<unsigned char *>(const_cast<typename ringCls::Part *>(_part_)) - *(unsigned char **)(&(field##_offset))/*offsetof(cls, field)*/); \
	} \
};

class PFApplAbilityUpgrade {
public:
    ring::Ring<PFApplAbilityUpgrade, int, int>::Part upgradesRing;
    seDECLARE_RING_CLASS_BASE(PFApplAbilityUpgrade, upgradesRing, Ring, int);
};
int main() {
    PFApplAbilityUpgrade::Ring::part(nullptr);
    return 0;
}