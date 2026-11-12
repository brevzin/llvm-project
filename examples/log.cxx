#include <meta>

enum class LogLevel {
    debug,
    info,
    error,
};

struct Log {
    consteval {
        for (std::meta::info e : enumerators_of(^^LogLevel)) {
            auto name = std::meta::id(identifier_of(e));
            queue_injection(^^{
                template <class... Args>
                __macro \(name)(this Log& self, std::string_view raw, Args&&... args) {
                    auto fs = substitute(^^::std::basic_format_string, {^^char, ^^Args...});

                    auto call_args = std::meta::list_builder(^^{ , });
                    ((call_args += ^^{ \(args) }), ...);

                    return ^^{
                        do {
                            constexpr std::string_view fmt = \(fs)(\(raw)).get();
                            \(self).do_log(::LogLevel::\(name), fmt, \(call_args));
                        }
                    };
                }
            });
        }
    }
};
