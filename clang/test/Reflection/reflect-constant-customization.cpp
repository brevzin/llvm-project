// RUN: %clang_cc1 -std=c++2d -freflection -verify %s

// P4340 ext: declaration surface for 'operator template'. See
// reflect-constant.md. This file tests only the declaration-side
// rules: recognized shapes, =default / =delete, access, and the effect on
// is_structural / usability as an NTTP that flows from the structural bit.

namespace std::meta {
  using info = decltype(^^::);
}

// ==== Recognized shapes ====

struct Ok1 {
  consteval operator template() const;
};
struct Ok2 {
  consteval operator template() const&;
};
struct Ok3 {
  consteval operator template(this Ok3 const&);
};
struct Ok4 {
  consteval operator template(this Ok4);
};

// The customization point may have any access.
class Private {
  consteval operator template() const;
};
class Protected {
protected:
  consteval operator template() const;
};

// ==== Defaulted and deleted ====

class Defaulted {
  int i;
public:
  consteval operator template() const = default;
};

struct Deleted {
  int i;
  consteval operator template() const = delete;
};

class DefaultedOutOfLine {
  int i;
public:
  consteval operator template() const;
};
consteval DefaultedOutOfLine::operator template() const = default;

// ==== Bad shapes ====

struct NotConsteval {
  constexpr operator template() const; // expected-error {{'operator template' must be a consteval member function}}
};

struct NotConst {
  consteval operator template(); // expected-error {{'operator template' must be const-qualified}}
};

struct RValueRef {
  consteval operator template() const&&; // expected-error {{'operator template' must not be &&-qualified}}
};

struct HasParams {
  consteval operator template(int) const; // expected-error {{'operator template' must take no parameters}}
};

struct WrongReturn {
  consteval int operator template() const; // expected-error {{'operator template' cannot have a return type}}
};

struct TemplateCP {
  template <class T>
  consteval operator template() const; // expected-error {{'operator template' must not be a template}}
};

struct BadExplicitObject {
  consteval operator template(this BadExplicitObject&&); // expected-error {{'operator template' must have an object parameter of type}}
};

struct WrongExplicitObjectType {
  consteval operator template(this int); // expected-error {{'operator template' must have an object parameter of type}}
};

// ==== Structural-type effects ====

// __metafunction(6, r) is detail::__metafn_is_structural_type; keep in sync
// with the metafunction table in ExprConstantMeta.cpp.
consteval bool is_structural(std::meta::info r) {
  return __metafunction(6, r);
}

struct NotOtherwiseStructural {
private:
  int i; // private member: not C++26-structural

public:
  consteval operator template() const;
};
static_assert(is_structural(^^NotOtherwiseStructural));

// Private customization points (user-provided or defaulted) still opt in.
class PrivateCustomized {
  int i;
  consteval operator template() const;
};
static_assert(is_structural(^^PrivateCustomized));

class PrivateDefaulted {
  int i;
  consteval operator template() const = default;
};
static_assert(is_structural(^^PrivateDefaulted));

class PrivateDefaultedOutOfLine {
  int i;
  consteval operator template() const;
};
consteval PrivateDefaultedOutOfLine::operator template() const = default;
static_assert(is_structural(^^PrivateDefaultedOutOfLine));

class PrivateDeleted {
  consteval operator template() const = delete;
};
static_assert(!is_structural(^^PrivateDeleted));

struct PlainStructural { int i; };
static_assert(is_structural(^^PlainStructural));

static_assert(!is_structural(^^Deleted));

// The opt-out is infectious.
struct HasDeletedMember {
  Deleted d;
};
static_assert(!is_structural(^^HasDeletedMember));

// A customized member makes an otherwise-plain aggregate still structural.
struct HasCustomizedMember {
  NotOtherwiseStructural m;
  int x;
};
static_assert(is_structural(^^HasCustomizedMember));

// Mutable members always disqualify, customization point or not.
struct MutableCustomized {
  mutable int i;
  consteval operator template() const;
};
static_assert(!is_structural(^^MutableCustomized));
