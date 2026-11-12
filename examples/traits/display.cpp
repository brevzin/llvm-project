// Copyright 2026 Jump Trading LLC
//
// Display: conditional conformance (Swift's 'extension Array: P where Element: P').

#include "traits.hpp"
#include <cassert>
#include <optional>
#include <string>
#include <vector>

trait!(Display, {
    auto fmt(std::string & out) const->void;
    auto to_string() const -> std::string {
        std::string s;
        self.fmt(s);
        return s;
    }
});

template <>
struct traits::impl<Display, int> {
    static auto fmt(int i, std::string& out) -> void { out += std::to_string(i); }
};

// conditional conformance: a vector<T> is Display when T is
template <traits::models<Display> T>
struct traits::impl<Display, std::vector<T>> {
    static auto fmt(std::vector<T> const& v, std::string& out) -> void {
        out += '[';
        for (bool first = true; auto const& e : v) {
            if (!std::exchange(first, false)) {
                out += ", ";
            }
            Display::fmt(e, out);
        }
        out += ']';
    }
};

template <traits::models<Display> T>
struct traits::impl<Display, std::optional<T>> {
    static auto fmt(std::optional<T> const& o, std::string& out) -> void {
        if (o) {
            out += "Some(";
            Display::fmt(*o, out);
            out += ')';
        }
        else {
            out += "None";
        }
    }
};

struct Point {
    int x, y;
    auto fmt(std::string& out) const -> void {
        out += "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
    }
};

static_assert(traits::models<std::vector<std::vector<int>>, Display>);
static_assert(not traits::models<std::vector<double>, Display>);  // double isn't Display

int main() {
    std::vector<std::optional<Point>> v = {Point{1, 2}, std::nullopt};
    assert(Display::to_string(v) == "[Some((1, 2)), None]");
    assert(Display::to_string(std::vector<std::vector<int>>{{1}, {2, 3}}) == "[[1], [2, 3]]");
}
