#include "input.hpp"
#include "config.hpp"

#include <linux/input.h>
#include <sys/time.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <glob.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace tv {

// 读一行文本（去掉行尾换行 / \r）
static std::string read_line(const std::string& path) {
    std::ifstream f(path);
    if (!f.good()) return "";
    std::string line;
    std::getline(f, line);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();
    return line;
}

InputHandler::InputHandler() {
    device_path_ = find_device();
    if (device_path_.empty()) {
        std::fprintf(stderr, "[Input] no ANBERNIC device found\n");
        return;
    }
    fd_ = ::open(device_path_.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd_ < 0) {
        std::fprintf(stderr, "[Input] open %s failed: %s\n",
                     device_path_.c_str(), std::strerror(errno));
        return;
    }
    std::fprintf(stderr, "[Input] using %s\n", device_path_.c_str());
}

InputHandler::~InputHandler() {
    if (fd_ >= 0) ::close(fd_);
}

std::string InputHandler::find_device() const {
    // 1) 遍历 /dev/input/event*，找 name 里含 ANBERNIC 的
    glob_t g;
    if (glob("/dev/input/event*", 0, nullptr, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc; i++) {
            std::string evpath = g.gl_pathv[i];
            auto slash = evpath.find_last_of('/');
            std::string devname = (slash == std::string::npos)
                                  ? evpath
                                  : evpath.substr(slash + 1);
            std::string sys_path = "/sys/class/input/" + devname + "/device/name";
            std::string name = read_line(sys_path);
            if (name.find("ANBERNIC") != std::string::npos) {
                globfree(&g);
                return evpath;
            }
        }
        globfree(&g);
    }

    // 2) 回退到 BOARD_MAPPING 里的编号
    std::string board = read_line("/mnt/vendor/oem/board.ini");
    if (board.empty()) board = "RG35xxH";
    int idx = 5;
    auto it = BOARD_MAPPING.find(board);
    if (it != BOARD_MAPPING.end()) idx = it->second;

    std::string fallback = "/dev/input/event" + std::to_string(idx);
    if (access(fallback.c_str(), R_OK) == 0) return fallback;
    return "";
}

void InputHandler::poll() {
    code_name_.clear();
    value_ = 0;
    if (fd_ < 0) return;

    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(fd_, &rset);
    struct timeval tv;
    tv.tv_sec  = 0;
    tv.tv_usec = 10000;   // 10ms

    int rc = select(fd_ + 1, &rset, nullptr, nullptr, &tv);
    if (rc <= 0) return;

    struct input_event ev;
    ssize_t n = ::read(fd_, &ev, sizeof(ev));
    if (n != (ssize_t)sizeof(ev)) return;

    // 只关心按键和绝对轴（方向键在 Anbernic 上走 EV_ABS）
    if (ev.type != EV_KEY && ev.type != EV_ABS) return;

    // 值 == 0 表示松开/归中，直接清空
    if (ev.value == 0) return;

    // 归一化：1 -> 1，其它（-1 或 2）-> -1
    int kv = (ev.value == 1) ? 1 : -1;

    auto it = KEYMAP.find(ev.code);
    code_name_ = (it != KEYMAP.end()) ? it->second : std::to_string(ev.code);
    value_ = kv;

    std::fprintf(stderr, "[Input] key=%-6s code=%u val=%d\n",
                 code_name_.c_str(), ev.code, kv);
}

} // namespace tv