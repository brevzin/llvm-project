// Copyright 2026 Jump Trading LLC
//
// Shape: structural and explicit conformance, defaults, return-type coercion, dyn.

#include "traits.hpp"
#include <cassert>
#include <format>
#include <string>
#include <vector>

trait!(Shape, {
    auto area() const -> double;
    auto name() const -> std::string;
    auto describe() const -> std::string {
        return std::format("{} with area {:.2f}", self.name(), self.area());
    }
});

struct Circle {
    float r;
    auto area() const -> float { return 3.14159f * r * r; }  // float: coerced to double
    auto name() const -> std::string { return "circle"; }
};

struct Square {
    int side;
    auto area() const -> int { return side * side; }
    auto name() const -> std::string { return "square"; }
    auto describe() const -> std::string { return "a humble square"; }  // overrides default
};

// a type we don't own, with no members
namespace geo {
struct Tri {
    double b, h;
};
}  // namespace geo
template <>
struct traits::impl<Shape, geo::Tri> {
    static auto area(geo::Tri const& t) -> double { return t.b * t.h / 2; }
    static auto name(geo::Tri const&) -> std::string { return "triangle"; }
};

struct NonConstArea {
    auto area() -> double { return 1; }  // not const: Shape demands const
    auto name() const -> std::string { return "?"; }
};

static_assert(traits::models<Circle, Shape>);
static_assert(traits::models<Square const&, Shape>);
static_assert(traits::models<geo::Tri, Shape>);
static_assert(!traits::models<NonConstArea, Shape>);
static_assert(traits::models<Shape::dyn, Shape>);
static_assert(std::same_as<decltype(Shape::area(Circle{1})), double>);

int main() {
    Circle c{1};
    Square s{2};
    geo::Tri t{3, 4};
    assert(Shape::describe(t) == "triangle with area 6.00");
    assert(Shape::describe(s) == "a humble square");

    std::vector<Shape::dyn> shapes = {c, s, t};
    double total = 0;
    for (auto& sh : shapes) {
        total += sh.area();
        std::puts(sh.describe().c_str());
    }
    assert(total > 13.1 && total < 13.2);
}
