#include <meta>

namespace lib {
    template <class E> requires std::is_enum_v<E>
    __macro bitmask_type() {
        auto ret = ^^{
            constexpr auto operator+(\(^^E) e) {
                return std::to_underlying(e);
            }
        };

        using enum std::meta::operators;
        for (auto op : {op_pipe, op_ampersand, op_caret}) {
            ret += ^^{
                constexpr auto operator \(op)(\(^^E) lhs, \(^^E) rhs) -> \(^^E) {
                    return \(^^E)(+lhs \(op) +rhs);
                }
            };
        }

        return ret;
    }
}

namespace N {
    enum class Permission : int {
        None    = 0,
        Read    = 1 << 0,
        Write   = 1 << 1,
        Execute = 1 << 2,
    };

    lib::bitmask_type!<Permission>();
}

static_assert((N::Permission::Read | N::Permission::Write) == N::Permission(3));
static_assert((N::Permission(7) & N::Permission::Read) == N::Permission::Read);
