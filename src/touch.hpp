#pragma once
#include <string>

namespace tv {

class TouchHandler {
public:
    TouchHandler(const std::string& device, int screen_w, int screen_h);
    ~TouchHandler();

    // 返回一次 tap (按下再抬起)；无事件返回 false
    bool get_tap(int& x, int& y);

    void set_screen_size(int w, int h);

private:
    int         fd_ = -1;
    int         screen_w_ = 640;
    int         screen_h_ = 480;
    int         last_x_ = -1;
    int         last_y_ = -1;
    bool        touch_active_ = false;
    bool        pending_ = false;
    int         pending_x_ = 0;
    int         pending_y_ = 0;
};

} // namespace tv