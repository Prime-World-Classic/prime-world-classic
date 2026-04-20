#include <stddef.h>
namespace ring {
    template <class T, class C, class R> struct Ring {
        static void part(T* obj) { C::part(obj); }
    };
}
class PFApplAbilityUpgrade {
public:
    int field;
    template <typename A> struct ring_Ring_;
    typedef ::ring::Ring<PFApplAbilityUpgrade, ring_Ring_<int>, int> Ring;
    template <typename A> struct ring_Ring_ {
        static void part(PFApplAbilityUpgrade* obj) {}
    };
};
int main() {
    PFApplAbilityUpgrade::Ring::part(nullptr);
    return 0;
}