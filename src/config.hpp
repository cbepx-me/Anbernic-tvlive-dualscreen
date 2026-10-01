#pragma once
#include <string>
#include <map>
#include <utility>

namespace tv {

inline constexpr const char* VERSION = "1.0.3";

// 机型 -> 编号
inline const std::map<std::string, int> BOARD_MAPPING = {
    {"RGds", 10},
    {"RGdsplus", 11},
};

// 编号 -> 单屏分辨率
inline const std::map<int, std::pair<int,int>> SCREEN_RES = {
    {10, {640, 480}},
    {11, {1024, 768}},
};

// Linux input event code -> 键名
inline const std::map<int, std::string> KEYMAP = {
    {304, "A"}, {305, "B"}, {306, "Y"}, {307, "X"},
    {308, "L1"}, {309, "R1"}, {314, "L2"}, {315, "R2"},
    {17, "DY"}, {16, "DX"},
    {310, "SELECT"}, {311, "START"}, {312, "MENUF"},
    {114, "V-"}, {115, "V+"},
};

inline constexpr const char* COLOR_BG          = "#0C121C";
inline constexpr const char* COLOR_BG_GRADIENT = "#182038";
inline constexpr const char* COLOR_TEXT        = "#E0E8F0";
inline constexpr const char* COLOR_SHADOW      = "#00000080";

extern std::string APP_PATH;
extern std::string FONT_FILE;

void init_paths();

// ============================
// 兼容旧代码：cfg.BOARD_MAPPING / cfg.KEYMAP / cfg.font_file 等
// ============================
class TVConfig {
public:
    const std::map<std::string, int>&        BOARD_MAPPING  = tv::BOARD_MAPPING;
    const std::map<int, std::pair<int,int>>& SCREEN_RES     = tv::SCREEN_RES;
    const std::map<int, std::string>&        KEYMAP         = tv::KEYMAP;
    const std::string&                       font_file      = tv::FONT_FILE;

    static constexpr const char* VERSION           = tv::VERSION;
    static constexpr const char* COLOR_BG          = tv::COLOR_BG;
    static constexpr const char* COLOR_BG_GRADIENT = tv::COLOR_BG_GRADIENT;
    static constexpr const char* COLOR_TEXT        = tv::COLOR_TEXT;
    static constexpr const char* COLOR_SHADOW      = tv::COLOR_SHADOW;

    TVConfig() = default;
};

} // namespace tv