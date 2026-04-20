template<class C> struct Ring {
    static void part() { C::part(); }
};

class A {
public:
    template<typename X> struct R_;
    typedef Ring<R_<int> > R;
    template<typename X> struct R_ {
        static void part() {}
    };
};

int main() {
    A::R::part();
}
