#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <chrono>
#include <meta>

using namespace std::literals;
using namespace std::chrono;

namespace i18n {

std::unordered_map<std::string_view, std::string_view> translations_de = {
    {"Hello, {}!", "Hallo, {}!"},
    {"You have {} new messages.", "Du hast {} neue Nachrichten."},
    {"The file {} has {} bytes.", "Die Datei {} hat {} Bytes."},
    {"{} sent you a message: {}", "{} hat dir eine Nachricht gesendet: {}"},
    {"Welcome, {}! You last logged in on {}.",
     "Willkommen, {}! Du hast dich zuletzt am {} angemeldet."},
    {"Copied {} files from {} to {}.",
     "{0} Dateien wurden von {1} nach {2} kopiert."},
    {"Dear {}, your order #{} will arrive on {}.",
     "Sehr geehrte(r) {0}, Ihre Bestellung #{1} wird am {2} eintreffen."},
    {"Error {}: Not found (file: {}, line: {}).",
     "Fehler {0}: Nicht gefunden (Datei: {1}, Zeile: {2})."},
};

std::unordered_map<std::string_view, std::string_view> translations_fr = {
    {"Hello, {}!", "Bonjour, {} !"},
    {"You have {} new messages.", "Vous avez {} nouveaux messages."},
    {"The file {} has {} bytes.", "Le fichier {} a {} octets."},
    {"{} sent you a message: {}", "{} vous a envoyé un message : {}"},
    {"Welcome, {}! You last logged in on {}.",
     "Bienvenue, {} ! Votre dernière connexion était le {}."},
    {"Copied {} files from {} to {}.",
     "{0} fichiers copiés de {1} vers {2}."},
    {"Dear {}, your order #{} will arrive on {}.",
     "Cher/Chère {0}, votre commande n°{1} arrivera le {2}."},
    {"Error {}: Not found (file: {}, line: {}).",
     "Erreur {0} : Non trouvé."},
};

std::unordered_map<std::string_view, std::string_view> translations_ja = {
    {"Hello, {}!", "こんにちは、{}さん！"},
    {"You have {} new messages.", "{}件の新しいメッセージがあります。"},
    {"The file {} has {} bytes.", "ファイル{}は{}バイトです。"},
    {"{} sent you a message: {}", "{}さんからメッセージが届きました：{}"},
    {"Welcome, {}! You last logged in on {}.",
     "ようこそ、{}さん！最終ログイン日時：{}"},
    {"Copied {} files from {} to {}.",
     "{1}から{2}へ{0}個のファイルをコピーしました。"},
    {"Dear {}, your order #{} will arrive on {}.",
     "{0}様、ご注文番号{1}は{2}に届く予定です。"},
    {"Error {}: Not found (file: {}, line: {}).",
     "エラー{0}: 見つかりません"},
};

enum class Language { English, German, French, Japanese };

auto gettext(std::string_view msgid, Language lang) -> std::string_view {
    std::unordered_map<std::string_view, std::string_view>* dict = nullptr;
    switch (lang) {
        case Language::English:
            return msgid;
        case Language::German:
            dict = &translations_de;
            break;
        case Language::French:
            dict = &translations_fr;
            break;
        case Language::Japanese:
            dict = &translations_ja;
            break;
    }
    if (dict) {
        auto it = dict->find(msgid);
        if (it != dict->end()) {
            return it->second;
        }
    }
    return msgid;
}

template <std::template_string S>
auto translate(S ts, Language lang) {
    // The translated pattern (a view into the catalog) rebound to the
    // template string's arguments; formatting stays deferred.
    return std::rebind_format(gettext(ts.fmt(), lang), std::move(ts));
}

}  // namespace i18n

i18n::Language the_lang;
#define _(msg) i18n::translate(msg, the_lang)

auto main() -> int {
    template for (constexpr auto e : define_static_array(enumerators_of(^^i18n::Language))) {
        std::println("=== {} ===", identifier_of(e));
        the_lang = [:e:];

        std::string_view name = "Alice";
        std::println(_(t"Hello, {name}!"));

        int count = 5;
        std::println(_(t"You have {count} new messages."));

        std::string_view filename = "report.pdf";
        int bytes = 1024;
        std::println(_(t"The file {filename} has {bytes} bytes."));

        std::string_view last_login = "2014-01-15";
        std::println(_(t"Welcome, {name}! You last logged in on {last_login}."));

        std::string_view source = "/home/data";
        std::string_view dest = "/backup";
        std::println(_(t"Copied {count} files from {source} to {dest}."));

        int order_id = 12345;
        sys_days date = 2026y/March/15;
        std::println(_(t"Dear {name}, your order #{order_id} will arrive on {date}."));

        int code = 404;
        filename = "main.cpp";
        int line = 42;
        std::println(_(t"Error {code}: Not found (file: {filename}, line: {line})."));

        std::println("");
    }
}
