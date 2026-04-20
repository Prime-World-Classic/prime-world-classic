#include <stddef.h>
namespace ring {
    template <class T, class C, class R> struct Ring {
        struct Part { int prev, next; };
        static void part(T* obj) { C::part(obj); }
    };
}
#define seDECLARE_RING_CLASS_BASE(cls, field, ringCls, refPolicy) \
template <typename A> struct ring ## _ ## ringCls ## _ { \
	static typename ::ring::Ring<cls, ring ## _ ## ringCls ## _<int>, refPolicy >::Part& part(cls * obj) { return obj->field; } \
}; \
typedef ::ring::Ring<cls, ring ## _ ## ringCls ## _<int>, refPolicy > ringCls;

class PFApplAbilityUpgrade {
public:
    ring::Ring<PFApplAbilityUpgrade, int, int>::Part upgradesRing;
    seDECLARE_RING_CLASS_BASE(PFApplAbilityUpgrade, upgradesRing, Ring, int);
};
int main() {
    PFApplAbilityUpgrade::Ring::part(nullptr);
    return 0;
}