#include <vector>
#include <meta>
#include <memory>

class C {
    std::unique_ptr<int> p_;

public:
    constexpr C(int i) : p_(new int(i)) {}
    constexpr auto get() -> int& { return *p_; }
    constexpr auto get() const -> int const& { return *p_; }
};

constexpr auto c = C(2);
static_assert(c.get() == 2);

// struct A {
//     std::vector<int> v;
//     int* p;
// };

// // a.p is a mutable pointer into the allocation
// constexpr A a = []{
//     std::vector<int> v = {1, 2, 3};
//     int* p = v.data();
//     return A{.v=std::move(v), .p=p};
// }();

// struct B {
//     int i;
//     int* p;

//     constexpr B(int i) : i(i), p(&this->i) { }
//     constexpr B(B const& rhs) : i(rhs.i), p(&i) { }
// };

// constexpr std::vector<B> bs = {1, 2, 3};

// auto main() -> int {
//     ++a.p[0];      // oops
//     ++bs[0].p[0];  // oops
// }
