#pragma once
#include <string>
#include <vector>
#include <memory>

#include "config.hpp"
#include "translator.hpp"
#include "input.hpp"
#include "touch.hpp"
#include "renderer.hpp"
#include "scanner.hpp"
#include "player.hpp"
#include "logo_loader.hpp"

namespace tv {

class TVApp {
public:
    TVApp();
    ~TVApp();
    void run();

private:
    void _load_config();
    void _save_config();
    void _ready();
    void _scan_sources();
    void _load_source(int idx);
    void _update_system_status();
    void _load_system_language();

    void _handle_input();
    void _change_channel(int delta);
    void _change_source(int delta);
    void play_selected(const std::string& sub_file = "");
    int  _page_size() const;
    std::string _make_sub();

    void _update(double dt);
    void _draw();
    void _draw_volume_bar(int W, int H, int bot_h);
    void _draw_upper_standby(bool bright);
    void _quit();

    // ★ 设计像素 → 实际像素
    int SC(int v) const { return (int)(v * ui_scale_ + 0.5f); }
    float ui_scale_ = 1.0f;

    TVConfig       cfg_;
    InputHandler   input_;
    std::unique_ptr<DualUIRenderer> ui_;
    TouchHandler   touch_;
    TVPlayer       player_;
    Translator     translator_;
    LogoLoader     logo_loader_;

    std::vector<Source>  sources_;
    int                  current_source_idx_  = 0;
    std::vector<Channel> current_channels_;
    int                  current_channel_idx_ = 0;
    int                  playing_channel_idx_ = -1;

    int    ui_display_    = 1;
    int    video_display_ = 0;

    std::string language_ = "zh_CN";
    std::vector<std::string> system_langs_;

    bool    show_hints_ = true;
    bool    last_show_hints_ = false;
    double  hint_timer_ = 15.0;
    static constexpr double HINT_TIMEOUT = 10.0;

    // ★ 音量条显示计时
    double  volume_show_timer_ = 0.0;
    static constexpr double VOLUME_SHOW_TIME = 2.0;

    bool        wifi_connected_   = false;
    std::string wifi_essid_;
    int         battery_level_    = 0;
    bool        battery_charging_ = false;
    double      status_timer_     = 0.0;

    std::string config_file_;
    bool        running_ = true;

    // ★ 启动时丢弃一次输入，避免残留事件误触发
    bool        skip_first_input_ = true;
};

} // namespace tv