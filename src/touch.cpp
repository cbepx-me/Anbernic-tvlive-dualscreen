#include "touch.hpp"

#include <linux/input.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <cerrno>

namespace tv {

TouchHandler::TouchHandler(const std::string& device, int screen_w, int screen_h)
    : screen_w_(screen_w), screen_h_(screen_h) {

    fd_ = ::open(device.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd_ < 0) {
        std::fprintf(stderr, "[Touch] open %s failed: %s\n",
                     device.c_str(), std::strerror(errno));
    } else {
        std::fprintf(stderr, "[Touch] opened %s (%dx%d)\n",
                     device.c_str(), screen_w_, screen_h_);
    }
}

TouchHandler::~TouchHandler() {
    if (fd_ >= 0) ::close(fd_);
}

void TouchHandler::set_screen_size(int w, int h) {
    screen_w_ = w;
    screen_h_ = h;
}

bool TouchHandler::get_tap(int& x, int& y) {
    if (fd_ < 0) return false;

    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(fd_, &rset);
    struct timeval tv {0, 0};
    if (select(fd_ + 1, &rset, nullptr, nullptr, &tv) <= 0) {
        if (pending_) {
            x = pending_x_;
            y = pending_y_;
            pending_ = false;
            return true;
        }
        return false;
    }

    char buf[256];
    ssize_t n = ::read(fd_, buf, sizeof(buf));
    if (n <= 0) return false;

    int off = 0;
    while (off + (int)sizeof(input_event) <= n) {
        input_event ev;
        std::memcpy(&ev, buf + off, sizeof(ev));
        off += sizeof(input_event);

        if (ev.type == EV_ABS) {
            if (ev.code == ABS_MT_POSITION_X) last_x_ = ev.value;
            else if (ev.code == ABS_MT_POSITION_Y) last_y_ = ev.value;
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            bool new_active = (ev.value == 1);
            if (!new_active && touch_active_) {
                if (last_x_ >= 0 && last_y_ >= 0) {
                    int xx = last_x_, yy = last_y_;
                    if (xx < 0) xx = 0;
                    if (yy < 0) yy = 0;
                    if (xx >= screen_w_) xx = screen_w_ - 1;
                    if (yy >= screen_h_) yy = screen_h_ - 1;
                    pending_x_ = xx;
                    pending_y_ = yy;
                    pending_ = true;
                }
            }
            touch_active_ = new_active;
        }
    }

    if (pending_) {
        x = pending_x_;
        y = pending_y_;
        pending_ = false;
        return true;
    }
    return false;
}

} // namespace tv