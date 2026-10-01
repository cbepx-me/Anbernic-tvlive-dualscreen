#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace tv {

class Translator {
public:
    // 加载 i18n.json 并设定当前语言
    void load(const std::string& lang_code = "zh_CN");

    // 切换语言（不重新读文件）
    void set_language(const std::string& lang_code);

    // 取翻译；找不到时先回退 en_US，再回退原 key
    std::string t(const std::string& key) const;

    const std::string& lang() const { return lang_code_; }

private:
    std::string       lang_code_ = "en_US";
    nlohmann::json    data_ = nlohmann::json::object();   // 整个 i18n.json
};

} // namespace tv