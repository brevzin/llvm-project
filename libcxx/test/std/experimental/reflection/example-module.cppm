module;
#include <experimental/meta>

export module Example;
namespace Example {
                              // ================
                              // Null reflections
                              // ================

export consteval auto rNull = decltype(^^::){};

                         // ===========================
                         // Reflections of type aliases
                         // ===========================

export using Alias = int;
export consteval auto rAlias = ^^Alias;

                           // ======================
                           // Reflections of objects
                           // ======================

static int obj = 13;
export consteval auto rObj = std::meta::reflect_object(obj);

                            // =====================
                            // Reflections of values
                            // =====================

export consteval auto rValue = std::meta::reflect_constant(1);
export consteval auto rRefl = std::meta::reflect_constant(rValue);
export consteval auto Splice = [:rRefl:];

                          // ========================
                          // Reflections of variables
                          // ========================

export int v42 = 42;
export consteval auto r42 = ^^v42;

                          // ========================
                          // Reflections of templates
                          // ========================

export template <auto V> int TVar = -V;
export consteval auto rTVar = ^^TVar;

export template <typename T, auto M> auto fn(const T &t) {
  return t.[:M:];
}

                          // =========================
                          // Reflections of namespaces
                          // =========================

export consteval auto rGlobalNS = ^^::;

                       // ==============================
                       // Reflections of base specifiers
                       // ==============================
export struct Empty {};
export struct Base {
  static constexpr int K = 12;
};
export struct Child : private Empty, Base {};
consteval auto ctx = std::meta::access_context::unchecked();
export consteval auto rBase1 = bases_of(^^Child, ctx)[0];
export consteval auto rBase2 = bases_of(^^Child, ctx)[1];

                      // =================================
                      // Reflections of data members specs
                      // =================================

export consteval auto rTDMS = data_member_spec(^^int, {.name="test"});

}  // namespace Example
