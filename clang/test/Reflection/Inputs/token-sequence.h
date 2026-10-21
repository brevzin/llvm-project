// A minimal std::meta::token_sequence, for tests that do not include <meta>:
// a token literal needs the class defined. The compiler gives the class its
// storage, and does these operators (and id) natively: they need no
// definitions.
namespace std::meta {
// (The compiler implements id, ignoring the declared return type: a token.)
template <class... Ts> consteval auto id(Ts const &...) -> int;
using token = decltype(id("x"));

class token_sequence {
public:
  constexpr token_sequence() noexcept = default;
  friend consteval auto operator+=(token_sequence &, const token_sequence &)
      -> token_sequence &;
  friend consteval auto operator+(token_sequence, const token_sequence &)
      -> token_sequence;
  friend consteval bool operator==(const token_sequence &,
                                   const token_sequence &);
  friend consteval bool operator==(token, const token_sequence &);
};
} // namespace std::meta
