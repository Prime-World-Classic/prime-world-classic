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
};

class PFApplAbilityUpgrade {
public:
    template <typename A> struct ring_Ring_;
    typedef ::ring::Ring<PFApplAbilityUpgrade, ring_Ring_<int>, int> Ring;
    template <typename A> struct ring_Ring_ {
        static typename Ring::Part& part(PFApplAbilityUpgrade * obj) { return obj->upgradesRing; }
    };
    Ring::Part upgradesRing;
};

void DoRing(PFApplAbilityUpgrade::Ring& r) {
    ring::Ring<PFApplAbilityUpgrade, PFApplAbilityUpgrade::ring_Ring_<int>, int>::part(nullptr);
}

int main() { return 0; }