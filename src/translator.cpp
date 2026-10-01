#include "translator.hpp"
#include "config.hpp"
#include <fstream>
#include <cstdio>

namespace tv {

void Translator::load(const std::string& lang_code) {
    lang_code_ = lang_code;

    // 只读一次；如果外部重复调 load，也不重复解析
    if (data_.is_object() && !data_.empty()) return;

    std::string path = APP_PATH + "/lang/i18n.json";
    std::ifstream f(path);
    if (!f.good()) {
        std::fprintf(stderr, "[Translator] %s not found\n", path.c_str());
        data_ = nlohmann::json::object();
        return;
    }

    data_ = nlohmann::json::parse(f, nullptr, false);
    if (data_.is_discarded() || !data_.is_object()) {
        std::fprintf(stderr, "[Translator] parse failed or wrong root type: %s\n",
                     path.c_str());
        data_ = nlohmann::json::object();
    } else {
        std::fprintf(stderr, "[Translator] loaded %s (%zu keys)\n",
                     path.c_str(), data_.size());
    }
}

void Translator::set_language(const std::string& lang_code) {
    lang_code_ = lang_code;
}

std::string Translator::t(const std::string& key) const {
    auto it = data_.find(key);
    if (it == data_.end() || !it->is_object()) {
        return key;                    // 根本没这个 key
    }
    const auto& entry = *it;

    // 1) 当前语言
    auto jt = entry.find(lang_code_);
    if (jt != entry.end() && jt->is_string()) return jt->get<std::string>();

    // 2) 回退 en_US
    auto et = entry.find("en_US");
    if (et != entry.end() && et->is_string()) return et->get<std::string>();

    // 3) 最后回退原 key
    return key;
}

} // namespace tv