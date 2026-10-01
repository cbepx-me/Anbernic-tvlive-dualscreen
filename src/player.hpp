#pragma once
#include <string>
#include <vector>
#include <sys/types.h>

namespace tv {

class TVPlayer {
public:
    TVPlayer(int video_screen_index);
    ~TVPlayer();

    void play(const std::string& url, const std::string& sub_file = "");
    void stop();
    bool is_alive();

    int  volume() const { return volume_; }
    void set_volume(int vol);

    const std::string& status() const { return status_; }
    const std::string& fail_reason() const { return fail_reason_; }

    void mark_failed() { status_ = "failed"; }

    void clear_failed() {
        if (status_ == "failed") status_ = "idle";
    }

    // ★ 新增：允许 App 在构造后修改 mpv 全屏的屏幕号
    void set_video_display(int idx) { video_display_ = idx; }

    // 清理：set_screen_name 不再需要
    // void set_screen_name(const std::string& name) { screen_name_ = name; }

private:
    void _load_volume();
    void _ipc_set_volume(int vol);

    pid_t       pid_ = -1;
    std::string status_ = "idle";
    std::string fail_reason_;
    std::string ipc_socket_;
    std::string last_url_;
    int         volume_ = 80;
    double      last_play_time_ = 0.0;
    int         video_display_ = 0;
    std::string mpv_log_file_;
    // 清理：std::string screen_name_;
};

} // namespace tv