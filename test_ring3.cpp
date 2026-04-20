#include <stddef.h>
namespace ring {
    template <class T, class C, class R> struct Ring {
        struct Part { int prev, next; };
        static void part(T* obj) { C::part(obj); }
    };
}
template <typename A> struct ring_Ring_;
typedef ::ring::Ring<class PFApplAbilityUpgrade, ring_Ring_<int>, int> Ring;
template <typename A> struct ring_Ring_ {
    static void part(PFApplAbilityUpgrade* obj) {}
};

int main() {
    Ring::part(nullptr);
    return 0;
}