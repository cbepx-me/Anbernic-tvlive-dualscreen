#include "app.hpp"

#include <SDL2/SDL.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <thread>

namespace tv {

static std::string ini_read(const std::string& path,
                            const std::string& section,
                            const std::string& key,
                            const std::string& def = "") {
    std::ifstream f(path);
    if (!f.good()) return def;
    std::string line, cur;
    std::string sec_hdr = "[" + section + "]";
    std::string kpat = key + "=";
    while (std::getline(f, line)) {
        size_t b = line.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) continue;
        size_t e = line.find_last_not_of(" \t\r\n");
        line = line.substr(b, e - b + 1);
        if (!line.empty() && line[0] == '[') { cur = line; continue; }
        if (cur == sec_hdr && line.rfind(kpat, 0) == 0)
            return line.substr(kpat.size());
    }
    return def;
}

static void ini_write(const std::string& path,
                      const std::string& section,
                      const std::string& key,
                      const std::string& value) {
    std::vector<std::string> lines;
    {
        std::ifstream f(path);
        std::string l;
        while (std::getline(f, l)) lines.push_back(l);
    }
    std::string sec_hdr = "[" + section + "]";
    std::string kpat = key + "=";
    int sec_idx = -1, key_idx = -1, next_sec = -1;
    std::string cur;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string s = lines[i];
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) continue;
        size_t e = s.find_last_not_of(" \t\r\n");
        s = s.substr(b, e - b + 1);
        if (!s.empty() && s[0] == '[') {
            if (cur == sec_hdr && next_sec < 0 && sec_idx >= 0) next_sec = (int)i;
            cur = s;
            if (cur == sec_hdr && sec_idx < 0) sec_idx = (int)i;
            continue;
        }
        if (cur == sec_hdr && s.rfind(kpat, 0) == 0) { key_idx = (int)i; break; }
    }
    if (key_idx >= 0) {
        lines[key_idx] = key + "=" + value;
    } else if (sec_idx >= 0) {
        int insert_at = (next_sec >= 0) ? next_sec : (int)lines.size();
        lines.insert(lines.begin() + insert_at, key + "=" + value);
    } else {
        if (!lines.empty()) lines.push_back("");
        lines.push_back(sec_hdr);
        lines.push_back(key + "=" + value);
    }
    std::ofstream f(path);
    for (auto& l : lines) f << l << "\n";
}

TVApp::TVApp()
    : touch_("/dev/input/event1", 640, 480),
      player_(0),
      logo_loader_(tv::APP_PATH + "/logo_cache") {

    system_langs_ = {"zh_CN", "zh_TW", "en_US", "ja_JP", "ko_KR",
                     "es_LA", "ru_RU", "de_DE", "fr_FR", "pt_BR"};

    std::string board = "RGds";
    {
        std::ifstream f("/mnt/vendor/oem/board.ini");
        if (f.good()) std::getline(f, board);
        while (!board.empty() && (board.back() == '\n' || board.back() == '\r'))
            board.pop_back();
        if (board.empty()) board = "RGds";
    }
    int hw = 10;
    auto it = BOARD_MAPPING.find(board);
    if (it != BOARD_MAPPING.end()) hw = it->second;
    std::fprintf(stderr, "[App] board=%s hw=%d\n", board.c_str(), hw);

    SDL_Init(SDL_INIT_VIDEO);
    int n = SDL_GetNumVideoDisplays();
    std::fprintf(stderr, "[App] displays=%d\n", n);

    // 打印每块屏的名字和物理位置
    //  Wayland 下 SDL_GetDisplayName 返回的就是 wl_output 名，
    //  例如 "HDMI-A-1"、"DSI-1"
    std::string upper_screen_name;
    for (int i = 0; i < n; i++) {
        SDL_Rect b;
        SDL_GetDisplayBounds(i, &b);
#if SDL_VERSION_ATLEAST(2,0,22)
        const char* nm = SDL_GetDisplayName(i);
#else
        const char* nm = nullptr;
#endif
        std::fprintf(stderr,
                     "[App] SDL display %d: name='%s' x=%d y=%d %dx%d\n",
                     i, nm ? nm : "(null)", b.x, b.y, b.w, b.h);
    }

    if (n >= 2) {
        ui_display_    = 1;   // SDL 编号 1 = 下屏
        video_display_ = 1;   // mpv 编号 1 = 上屏
    } else {
        ui_display_ = 0;
        video_display_ = 0;
    }
    SDL_Quit();

    player_.set_video_display(video_display_);
    std::fprintf(stderr, "[App] player video display = %d\n", video_display_);

    ui_.reset(new DualUIRenderer());
    if (!ui_->ok()) {
        std::fprintf(stderr, "[App] UI init failed\n");
        running_ = false;
        return;
    }
    ui_->set_target(ui_display_);

    auto sz = ui_->size(ui_display_);
    touch_.set_screen_size(sz.first, sz.second);

    // ★ 自适应缩放：以 640×480 为设计基准
    ui_scale_ = std::min(sz.first  / 640.0f,
                         sz.second / 480.0f);
    if (ui_scale_ < 0.75f) ui_scale_ = 0.75f;

    std::fprintf(stderr, "[App] UI screen %d (%dx%d) scale=%.2f\n",
                 ui_display_, sz.first, sz.second, ui_scale_);

    config_file_ = tv::APP_PATH + "/tv.ini";
    _load_config();

    _load_system_language();
    translator_.load(language_);

    _ready();
    _scan_sources();
    _update_system_status();

    last_show_hints_ = !show_hints_;
}

TVApp::~TVApp() {
    player_.stop();
}

void TVApp::_load_config() {
    std::string vs = ini_read(config_file_, "Volume", "value", "");
    if (!vs.empty()) {
        int v = std::atoi(vs.c_str());
        if (v >= 0 && v <= 130) player_.set_volume(v);
    }
    std::string ss = ini_read(config_file_, "Resume", "source_index", "");
    if (!ss.empty()) {
        int s = std::atoi(ss.c_str()) - 1;
        if (s >= 0) current_source_idx_ = s;
    }
    std::string cs = ini_read(config_file_, "Resume", "channel_index", "");
    if (!cs.empty()) {
        int c = std::atoi(cs.c_str()) - 1;
        if (c >= 0) current_channel_idx_ = c;
    }
}

void TVApp::_load_system_language() {
    // 优先 /mnt/vendor/oem/language.ini（数字索引）
    {
        std::ifstream lf("/mnt/vendor/oem/language.ini");
        if (lf.good()) {
            std::string l;
            if (std::getline(lf, l)) {
                int idx = std::atoi(l.c_str());
                if (idx >= 0 && idx < (int)system_langs_.size()) {
                    language_ = system_langs_[idx];
                    std::fprintf(stderr, "[App] lang from system file: %s\n",
                                 language_.c_str());
                    return;
                }
            }
        }
    }
    // 回退到环境变量
    const char* env = std::getenv("LANG");
    if (env && *env) {
        std::string e = env;
        auto dot = e.find('.');
        if (dot != std::string::npos) e = e.substr(0, dot);
        for (auto& l : system_langs_) {
            if (l == e) { language_ = l; return; }
        }
        // zh-CN / zh-CN 之类
        std::string e2 = e;
        std::replace(e2.begin(), e2.end(), '-', '_');
        for (auto& l : system_langs_) {
            if (l == e2) { language_ = l; return; }
        }
    }
    std::fprintf(stderr, "[App] lang fallback: %s\n", language_.c_str());
}

void TVApp::_save_config() {
    ini_write(config_file_, "Volume", "value", std::to_string(player_.volume()));
    ini_write(config_file_, "Resume", "source_index", std::to_string(current_source_idx_ + 1));
    ini_write(config_file_, "Resume", "channel_index", std::to_string(current_channel_idx_ + 1));
}

// 判断路径是否是一个独立挂载点
static bool is_mount_point(const std::string& p) {
    struct stat st1, st2;
    if (stat(p.c_str(), &st1) != 0) return false;
    if (stat("/", &st2) != 0) return false;
    return st1.st_dev != st2.st_dev;
}

static void ensure_dir(const std::string& p) {
    // 已存在时 mkdir 会失败，忽略即可
    if (mkdir(p.c_str(), 0755) == 0)
        std::fprintf(stderr, "[App] created dir: %s\n", p.c_str());
}

void TVApp::_ready() {
    const char* bases[] = {
        "/mnt/mmc",
        "/mnt/sdcard",
        nullptr
    };

    for (int i = 0; bases[i]; i++) {
        std::string b = bases[i];
        // /mnt/sdcard 没挂载就跳过（避免在只读 rootfs 上创建失败）
        if (b == "/mnt/sdcard" && !is_mount_point(b)) continue;
        ensure_dir(b + "/TV");
    }

    // 程序自身目录下也建一个
    if (!tv::APP_PATH.empty())
        ensure_dir(tv::APP_PATH + "/TV");
}

void TVApp::_scan_sources() {
    sources_ = TVScanner::find_sources();
    if (sources_.empty()) {
        std::fprintf(stderr, "[App] no sources\n");
        return;
    }
    if (current_source_idx_ < 0 || current_source_idx_ >= (int)sources_.size())
        current_source_idx_ = 0;
    _load_source(current_source_idx_);
}

void TVApp::_load_source(int idx) {
    if (idx < 0 || idx >= (int)sources_.size()) return;
    current_channels_ = TVScanner::scan_source(sources_[idx]);
    current_source_idx_ = idx;
    if (current_channel_idx_ < 0 || current_channel_idx_ >= (int)current_channels_.size())
        current_channel_idx_ = 0;
    std::fprintf(stderr, "[App] loaded %s (%zu ch)\n",
                 sources_[idx].name.c_str(), current_channels_.size());
}

void TVApp::_update_system_status() {
    // ---------------- 电池 ----------------
    battery_level_    = 0;
    battery_charging_ = false;

    const char* bp[] = {
        "battery",
        "BAT0",
        "axp2202-battery",
        nullptr
    };
    for (int i = 0; bp[i]; i++) {
        std::string base = std::string("/sys/class/power_supply/") + bp[i];

        // capacity
        {
            std::ifstream f(base + "/capacity");
            if (f.good()) {
                int v = 0;
                if (f >> v) {
                    battery_level_ = v;
                    // status（充电状态）
                    std::ifstream fs(base + "/status");
                    std::string st;
                    if (fs.good() && std::getline(fs, st)) {
                        battery_charging_ = (st == "Charging" || st == "Full");
                    }
                    break;   // 找到电池了，跳出
                }
            }
        }
    }

    // ---------------- WiFi ----------------
    wifi_connected_ = false;
    wifi_essid_.clear();

    // 先看 operstate
    {
        std::ifstream f("/sys/class/net/wlan0/operstate");
        std::string l;
        if (f.good() && std::getline(f, l))
            wifi_connected_ = (l == "up");
    }

    // 如果连接着，读 SSID
    if (wifi_connected_) {
        FILE* fp = popen("iw dev wlan0 link 2>/dev/null", "r");
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                std::string s(line);
                auto p = s.find("SSID:");
                if (p == std::string::npos) continue;

                std::string essid = s.substr(p + 5);

                // 去掉首尾空白（含 \n \r \t 空格）
                auto is_ws = [](char c) {
                    return c == ' ' || c == '\t' ||
                           c == '\n' || c == '\r';
                };
                while (!essid.empty() && is_ws(essid.front()))
                    essid.erase(essid.begin());
                while (!essid.empty() && is_ws(essid.back()))
                    essid.pop_back();

                wifi_essid_ = essid;
                break;
            }
            pclose(fp);
        }
    }

    std::fprintf(stderr, "[App] status: bat=%d%% %s  wifi=%d ssid='%s'\n",
                 battery_level_,
                 battery_charging_ ? "(chg)" : "",
                 (int)wifi_connected_,
                 wifi_essid_.c_str());
}

void TVApp::_handle_input() {
    // ★ 启动后第一帧：清空输入缓冲，丢弃残留事件
    if (skip_first_input_) {
        skip_first_input_ = false;
        input_.reset();
        return;
    }

    input_.poll();
    if (input_.code_name().empty()) return;

    std::string k = input_.code_name();
    int val = input_.value();

    if (show_hints_) hint_timer_ = HINT_TIMEOUT;

    if (k == "SELECT") { _quit(); return; }

    if (k == "V-") {
        player_.set_volume(player_.volume() - 5);
        if (show_hints_) volume_show_timer_ = VOLUME_SHOW_TIME;   // ★ 触发音量条
        _save_config();
        return;
    }
    if (k == "V+") {
        player_.set_volume(player_.volume() + 5);
        if (show_hints_) volume_show_timer_ = VOLUME_SHOW_TIME;
        _save_config();
        return;
    }
    if (k == "X") {
        show_hints_ = !show_hints_;
        hint_timer_ = HINT_TIMEOUT;
        return;
    }
    if (!show_hints_) {
        if (k == "B") {
            show_hints_ = true;
            hint_timer_ = HINT_TIMEOUT;
            return;
        }
        return;
    }

    if (k == "L1") {
        _change_source(-1);
        if (player_.status() == "playing") player_.stop();
    } else if (k == "R1") {
        _change_source(1);
        if (player_.status() == "playing") player_.stop();
    } else if (k == "B") {
        player_.stop();
        playing_channel_idx_ = -1;
    } else if (k == "A") {
        if (player_.status() != "playing" ||
            playing_channel_idx_ != current_channel_idx_) {
            play_selected(_make_sub());
        }
    } else if (k == "Y") {
        _scan_sources();
        player_.clear_failed();
    } else if (k == "DY") {
        if (val == -1) _change_channel(-1);
        else if (val == 1) _change_channel(1);
    } else if (k == "DX") {
        int page = _page_size();
        if (val == -1) _change_channel(-page);
        else if (val == 1) _change_channel(page);
    }
}

void TVApp::_change_channel(int delta) {
    if (current_channels_.empty()) return;
    int n = (int)current_channels_.size();
    current_channel_idx_ = ((current_channel_idx_ + delta) % n + n) % n;
    if (player_.is_alive()) {
        play_selected(_make_sub());   // 播放器还活着 → 跟着换台
    } else {
        player_.clear_failed();       // ★ 新增：没在播 → 清掉失败提示
    }
}

void TVApp::_change_source(int delta) {
    if (sources_.empty()) return;
    int n = (int)sources_.size();
    current_source_idx_ = ((current_source_idx_ + delta) % n + n) % n;
    _load_source(current_source_idx_);
    current_channel_idx_ = 0;
    if (player_.status() == "playing") {
        player_.stop();
        playing_channel_idx_ = -1;
    }
    player_.clear_failed();
}

void TVApp::play_selected(const std::string& sub_file) {
    if (current_channels_.empty() ||
        current_channel_idx_ >= (int)current_channels_.size()) return;
    auto& c = current_channels_[current_channel_idx_];
    player_.play(c.url, sub_file);
    if (player_.status() == "playing")
        playing_channel_idx_ = current_channel_idx_;
    else
        playing_channel_idx_ = -1;
}

int TVApp::_page_size() const {
    auto sz = ui_->size(ui_display_);
    int H = sz.second;

    int top_h  = SC(44);
    int bot_h  = SC(56);
    int item_h = SC(28);

    int ly     = top_h + SC(8);
    int lh     = H - ly - bot_h - SC(16);
    int list_h = lh - SC(54);

    if (list_h <= 0) return 1;
    return std::max(1, list_h / item_h);
}

std::string TVApp::_make_sub() {
    std::string sub_file = tv::APP_PATH + "/tv.srt";
    if (current_channels_.empty()) return sub_file;
    auto& c = current_channels_[current_channel_idx_];
    std::string line_info = "Group " + c.group;
    if (c.line_total > 1)
        line_info += "  |  Line " + std::to_string(c.line_no) + "/" + std::to_string(c.line_total);
    std::string txt = c.name + " (" + line_info + ")";
    std::ofstream f(sub_file);
    f << "1\n00:00:00,000 --> 00:00:05,000\n" << txt << "\n";
    return sub_file;
}

void TVApp::_draw_upper_standby(bool bright) {
    if (ui_->num_screens() < 2) return;

    // ★ 用 SDL 编号：上屏永远是 ui_display_ 之外的那块
    int target_disp = (ui_display_ == 0) ? 1 : 0;
    if (target_disp < 0 || target_disp >= ui_->num_screens())
        return;   // 只有一块屏，什么都不做

    ui_->set_target(target_disp);
    auto sz = ui_->size(target_disp);
    int W = sz.first, H = sz.second;

    // 副屏单独算缩放
    float s = std::min(W / 640.0f, H / 480.0f);
    if (s < 0.75f) s = 0.75f;
    auto SS = [s](int v) { return (int)(v * s + 0.5f); };

    uint32_t main_col = bright ? rgba(0x4F, 0xC3, 0xF7) : rgba(0x1C, 0x26, 0x3B);
    uint32_t sub_col  = bright ? rgba(0x90, 0xCA, 0xF9) : rgba(0x1E, 0x26, 0x36);

    ui_->fill_gradient_v(0, 0, W, H,
                         rgba(0x18, 0x20, 0x38),
                         rgba(0x0C, 0x12, 0x1C), target_disp);
    ui_->text(W/2, H/2 - SS(20), "Anbernic", SS(72), main_col, "mm", false, target_disp);
    ui_->text(W/2, H/2 + SS(50), "TVLive",   SS(24), sub_col,  "mm", false, target_disp);
    ui_->paint(target_disp);
    ui_->set_target(ui_display_);
}

void TVApp::_update(double dt) {
    logo_loader_.process_pending();

    status_timer_ += dt;
    if (status_timer_ >= 10.0) {
        status_timer_ = 0;
        _update_system_status();
    }

    if (show_hints_) {
        hint_timer_ -= dt;
        if (hint_timer_ <= 0) show_hints_ = false;
    }

    // ★ 音量条倒计时
    if (volume_show_timer_ > 0.0) {
        volume_show_timer_ -= dt;
        if (volume_show_timer_ < 0.0) volume_show_timer_ = 0.0;
    }

    if (player_.status() == "playing" && !player_.is_alive()) {
        player_.mark_failed();
        playing_channel_idx_ = -1;
    }

    if (show_hints_ != last_show_hints_) {
        last_show_hints_ = show_hints_;
        if (player_.status() == "idle" && ui_->num_screens() >= 2) {
            _draw_upper_standby(show_hints_);
        }
    }
}

void TVApp::_draw_volume_bar(int W, int H, int bot_h) {
    int v = player_.volume();
    int vw = SC(340), vh = SC(56);
    int vx = (W - vw) / 2;
    int vy = H - bot_h - SC(90);

    // 半透明底板
    ui_->fill_rounded_rect(vx, vy, vx + vw, vy + vh, SC(12),
                           rgba(0x0A, 0x14, 0x22, 230));

    // 音量数字
    char vbuf[16];
    std::snprintf(vbuf, sizeof(vbuf), "%d", v);
    ui_->text(vx + SC(22), vy + vh/2, vbuf, SC(20),
              rgba(0xE0, 0xE8, 0xF0), "lm");

    // 进度条底槽
    int bar_x = vx + SC(70);
    int bar_y = vy + vh/2 - SC(7);
    int bar_w = vw - SC(70) - SC(22);
    int bar_h = SC(14);
    ui_->fill_rounded_rect(bar_x, bar_y, bar_x + bar_w, bar_y + bar_h,
                           SC(7), rgba(0x1E, 0x2A, 0x3C));

    // 填充
    int fill = bar_w * v / 130;
    if (fill > 0) {
        uint32_t col = v >= 100 ? rgba(0xEF, 0x53, 0x50) :
                       v >= 70  ? rgba(0xFF, 0xB7, 0x4D) :
                                  rgba(0x4F, 0xC3, 0xF7);
        ui_->fill_rounded_rect(bar_x, bar_y, bar_x + fill, bar_y + bar_h,
                               SC(7), col);
    }
}

void TVApp::_draw() {
    auto sz = ui_->size(ui_display_);
    int W = sz.first, H = sz.second;
    ui_->set_target(ui_display_);
    ui_->clear();

    if (!show_hints_) {
        ui_->fill_rect(0, 0, W, H, rgba(0, 0, 0, 200));
        std::string off_txt = translator_.t("Screen off playing") +
                              " · " +
                              translator_.t("Press B or X to wake");
        ui_->text(W/2, H/2 - SC(30), off_txt,
                  SC(28), rgba(0x3A, 0x3A, 0x3A), "mm");
        ui_->paint();

        // 就算息屏也允许显示音量条（可选）
        if (volume_show_timer_ > 0.0) {
            _draw_volume_bar(W, H, SC(44));
            ui_->paint();
        }
        return;
    }

    int top_h = SC(44), bot_h = SC(56);

    // 顶部状态栏
    ui_->fill_rect(0, 0, W, top_h, rgba(0x0A, 0x10, 0x20));
    ui_->text(SC(12), SC(12), translator_.t("TVLive") + " v" + VERSION,
              SC(18), rgba(0xE0, 0xE8, 0xF0), "lt");

    // ============ 右侧状态栏（从右往左排） ============
    // 全部用 "rm" anchor（右-中），每个元素画完后
    // cur_x 减去 该元素宽度 + 间距，下一个元素接在左边。
    int cur_x  = W - SC(12);
    int mid_y  = SC(22);          // 状态栏垂直中线
    int gap    = SC(16);          // 元素之间的水平间距

    // ---------- ① 电量（最右） ----------
    {
        char bat[32];
        std::snprintf(bat, sizeof(bat), "%d%%%s",
                      battery_level_,
                      battery_charging_ ? " █" : "");
        uint32_t bat_col = battery_level_ >= 60 ? rgba(0x4F, 0xC3, 0xF7) :
                           battery_level_ >= 20 ? rgba(0x64, 0xF6, 0xA6) :
                                                  rgba(0xEF, 0x53, 0x50);
        ui_->text(cur_x, mid_y, bat, SC(18), bat_col, "rm");

        auto [tw, th] = ui_->measure_text(bat, SC(18));
        (void)th;
        cur_x -= (tw + gap);
    }

    // ---------- ② 时间 ----------
    {
        std::time_t tt = std::time(nullptr);
        std::tm tm_buf{};
        localtime_r(&tt, &tm_buf);
        char time_str[16];
        std::strftime(time_str, sizeof(time_str), "%H:%M", &tm_buf);

        ui_->text(cur_x, mid_y, time_str, SC(18),
                  rgba(0xE0, 0xE8, 0xF0), "rm");

        auto [tw, th] = ui_->measure_text(time_str, SC(18));
        (void)th;
        cur_x -= (tw + gap);
    }

    // ---------- ③ WiFi（最左） ----------
    {
        std::string wifi = wifi_connected_
                           ? (translator_.t("WiFi:") + " " + wifi_essid_)
                           : translator_.t("WiFi ×");

        // 太长就截断，避免压到 "TVLive v1.0.0"
        if (wifi.size() > 24) wifi = wifi.substr(0, 22) + "...";

        uint32_t wifi_col = wifi_connected_ ? rgba(0x4F, 0xC3, 0xF7)
                                            : rgba(0xEF, 0x53, 0x50);
        ui_->text(cur_x, mid_y, wifi, SC(16), wifi_col, "rm");
    }

    // 左侧列表
    int lx = SC(12), ly = top_h + SC(8);
    int lw = (int)(W * 0.40);
    int lh = H - ly - bot_h - SC(16);
    ui_->fill_rounded_rect(lx, ly, lx + lw, ly + lh, SC(8), rgba(0x0F, 0x1A, 0x2E));

    std::string src_name = sources_.empty()
                           ? translator_.t("No source")
                           : sources_[current_source_idx_].name;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s  (%zu%s)",
                  src_name.c_str(),
                  current_channels_.size(),
                  translator_.t("channels").c_str());
    ui_->text(lx + SC(12), ly + SC(8), buf, SC(18), rgba(0x64, 0xB5, 0xF6), "lt");
    std::snprintf(buf, sizeof(buf), "%d/%zu %s",
                  current_source_idx_ + 1, sources_.size(),
                  translator_.t("Source").c_str());
    ui_->text(lx + SC(12), ly + SC(34), buf, SC(14), rgba(0x7A, 0x8B, 0xA0), "lt");

    int item_h = SC(28);
    int list_top = ly + SC(54);
    int visible = _page_size();
    int start = current_channel_idx_ - visible / 2;
    if (start < 0) start = 0;
    if (current_channels_.size() > (size_t)visible &&
        start > (int)current_channels_.size() - visible)
        start = (int)current_channels_.size() - visible;

    for (int i = 0; i < visible; i++) {
        int idx = start + i;
        if (idx >= (int)current_channels_.size()) break;
        int y = list_top + i * item_h;
        if (idx == current_channel_idx_) {
            ui_->fill_rounded_rect(lx + SC(6), y, lx + lw - SC(6),
                                   y + item_h - SC(2), SC(4),
                                   rgba(0x1E, 0x88, 0xE5));
        }
        uint32_t col = (idx == current_channel_idx_)
                       ? rgba(0xFF, 0xFF, 0xFF) : rgba(0xB0, 0xC4, 0xDE);
        auto& c = current_channels_[idx];
        std::string label = std::to_string(idx + 1) + " " + c.name;
        if (c.line_total > 1)
            label += "  " + std::to_string(c.line_no) + "/" + std::to_string(c.line_total);
        ui_->text(lx + SC(12), y + SC(4), label, SC(16), col, "lt");
    }

    // 右侧面板
    int rx = lx + lw + SC(12);
    int rw = W - rx - SC(12);
    int rh = lh;
    ui_->fill_rounded_rect(rx, ly, rx + rw, ly + rh, SC(8), rgba(0x0F, 0x1A, 0x2E));

    int info_y = ly + rh / 2 + SC(20);

    if (!current_channels_.empty()) {
        auto& cur = current_channels_[current_channel_idx_];

        int pad = SC(14);
        int logo_h = std::min(SC(200), std::max(SC(140), rh / 2 - SC(70)));
        int logo_w = rw - pad * 2;
        int lx0 = rx + pad;
        int ly0 = ly + pad;

        ui_->fill_rounded_rect(lx0, ly0, lx0 + logo_w, ly0 + logo_h, SC(10),
                               rgba(0x0A, 0x14, 0x24));

        if (!cur.logo.empty()) {
            SDL_Surface* img = logo_loader_.get(cur.logo,
                                                logo_w - SC(16), logo_h - SC(16));
            if (img) {
                ui_->draw_image_fit(img, lx0 + logo_w/2, ly0 + logo_h/2,
                                    logo_w - SC(16), logo_h - SC(16));
            } else {
                std::string msg = logo_loader_.is_loading(cur.logo)
                                  ? translator_.t("loading...")
                                  : translator_.t("no logo");
                ui_->text(lx0 + logo_w/2, ly0 + logo_h/2, msg,
                          SC(16), rgba(0x5A, 0x6A, 0x7F), "mm");
            }
        } else {
            ui_->text(lx0 + logo_w/2, ly0 + logo_h/2,
                      translator_.t("no logo"),
                      SC(16), rgba(0x5A, 0x6A, 0x7F), "mm");
        }

        info_y = ly0 + logo_h + SC(24);
        std::string name = cur.name;
        if (name.size() > 40) name = name.substr(0, 38) + "...";
        ui_->text(rx + rw/2, info_y, name, SC(22), rgba(0xE0, 0xE8, 0xF0), "mm");
        std::string meta = translator_.t("Group:") + " " + cur.group;
        if (cur.line_total > 1)
            meta += "   " + translator_.t("Line:") + " "
                  + std::to_string(cur.line_no) + "/" + std::to_string(cur.line_total);
        ui_->text(rx + rw/2, info_y + SC(28), meta, SC(14), rgba(0x7A, 0x8B, 0xA0), "mm");

        int btn_w = SC(200), btn_h = SC(40);
        int btn_x = rx + (rw - btn_w) / 2;
        int btn_y = ly + rh - SC(60);
        ui_->fill_rounded_rect(btn_x, btn_y, btn_x + btn_w, btn_y + btn_h, SC(8),
                               rgba(0x1E, 0x3A, 0x5F));
        ui_->text(btn_x + btn_w/2, btn_y + btn_h/2,
                  translator_.t("A Play"),
                  SC(18), rgba(0xB0, 0xC4, 0xDE), "mm");
    } else {
        ui_->text(rx + rw/2, ly + rh/2, translator_.t("No channels"),
                  SC(22), rgba(0x7A, 0x8B, 0xA0), "mm");
    }

    // 播放状态覆盖
    std::string st = player_.status();
    if (st == "playing" || st == "connecting" || st == "failed") {
        std::string txt;
        if (st == "playing") txt = translator_.t("Playing");
        else if (st == "connecting") txt = translator_.t("Connecting...");
        else txt = translator_.t("Playback failed");
        uint32_t col = (st == "playing")
                       ? rgba(0x66, 0xBB, 0x6A) : rgba(0xFF, 0x6B, 0x6B);
        ui_->text(rx + rw/2, info_y + SC(56), txt, SC(22), col, "mm");
    }

    if (volume_show_timer_ > 0.0) {
        _draw_volume_bar(W, H, bot_h);
    }

    // 底部提示
    ui_->fill_rect(0, H - bot_h, W, H, rgba(0x0A, 0x10, 0x20));
    std::string hint1 =
        std::string("↑↓ ") + translator_.t("Channel") +
        "  ←→ " + translator_.t("Page") +
        "  L1/R1 " + translator_.t("Source") +
        "  SEL " + translator_.t("Exit");
    std::string hint2 =
        std::string("A ") + translator_.t("Play") +
        "  B " + translator_.t("Stop") +
        "  X " + translator_.t("Screen off") +
        "  Y " + translator_.t("Refresh");

    ui_->text(SC(12), H - bot_h + SC(6),  hint1, SC(14),
              rgba(0xB0, 0xC4, 0xDE), "lt");
    ui_->text(SC(12), H - bot_h + SC(28), hint2, SC(14),
              rgba(0xB0, 0xC4, 0xDE), "lt");

    ui_->paint();
}

void TVApp::_quit() {
    _save_config();
    player_.stop();
    ui_->draw_end();
    running_ = false;
    std::exit(0);
}

void TVApp::run() {
    if (ui_->num_screens() >= 2) _draw_upper_standby(true);

    auto last = std::chrono::steady_clock::now();
    while (running_) {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;

        // 触摸
        int tx, ty;
        if (touch_.get_tap(tx, ty)) {
            auto sz = ui_->size(ui_display_);
            int W = sz.first, H = sz.second;
            int lw = (int)(W * 0.40);
            if (tx < lw) {
                int top_h = SC(44);
                int ly = top_h + SC(8);
                int item_h = SC(28);
                int list_top = ly + SC(54);
                int visible = _page_size();
                int start = current_channel_idx_ - visible / 2;
                if (start < 0) start = 0;
                for (int i = 0; i < visible; i++) {
                    int idx = start + i;
                    if (idx >= (int)current_channels_.size()) break;
                    int ry = list_top + i * item_h;
                    if (ry <= ty && ty <= ry + item_h - SC(2)) {
                        current_channel_idx_ = idx;
                        if (player_.is_alive()) play_selected(_make_sub());
                        break;
                    }
                }
            } else {
                int rx = lw + SC(12);
                int rw = W - rx - SC(12);
                int ly = SC(44) + SC(8);
                int rh = H - ly - SC(56) - SC(16);
                int btn_w = SC(200), btn_h = SC(40);
                int bx = rx + (rw - btn_w) / 2;
                int by = ly + rh - SC(60);
                if (bx <= tx && tx <= bx + btn_w &&
                    by <= ty && ty <= by + btn_h) {
                    if (player_.status() != "playing" ||
                        playing_channel_idx_ != current_channel_idx_) {
                        play_selected(_make_sub());
                    }
                }
            }
        }

        _handle_input();
        _update(dt);
        _draw();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

} // namespace tv