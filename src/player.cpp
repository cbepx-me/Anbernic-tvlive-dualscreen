#include "player.hpp"
#include "config.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>

namespace tv {

TVPlayer::TVPlayer(int video_screen_index) {
    video_display_ = video_screen_index;
    mpv_log_file_ = "/tmp/mpv_tv.log";

    char buf[64];
    std::snprintf(buf, sizeof(buf), "/tmp/mpv-tv-%d.sock", (int)getpid());
    ipc_socket_ = buf;
    ::unlink(ipc_socket_.c_str());

    _load_volume();
}

TVPlayer::~TVPlayer() { stop(); }

void TVPlayer::_load_volume() {
    std::string vf = APP_PATH + "/volume.txt";
    std::ifstream f(vf);
    if (f.good()) {
        int v = 80;
        f >> v;
        if (v >= 0 && v <= 130) volume_ = v;
    }
}

void TVPlayer::set_volume(int vol) {
    volume_ = std::max(0, std::min(130, vol));
    if (pid_ > 0 && is_alive()) {
        _ipc_set_volume(volume_);
    }
}

void TVPlayer::_ipc_set_volume(int vol) {
    if (ipc_socket_.empty()) return;
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return;

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, ipc_socket_.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        std::ostringstream oss;
        oss << "{\"command\":[\"set_property\",\"volume\"," << vol << "]}\n";
        std::string payload = oss.str();
        ssize_t w = ::write(fd, payload.data(), payload.size());
        (void)w;
        std::fprintf(stderr, "[Player] ipc set volume -> %d\n", vol);
    }
    ::close(fd);
}

void TVPlayer::play(const std::string& url, const std::string& sub_file) {
    double now = (double)time(nullptr);
    if (url == last_url_ && now - last_play_time_ < 1.0) {
        std::fprintf(stderr, "[Player] skip duplicate play\n");
        return;
    }
    stop();
    ::unlink(ipc_socket_.c_str());

    status_ = "connecting";
    fail_reason_.clear();
    last_url_ = url;
    last_play_time_ = now;

    std::vector<std::string> args = {
        "mpv",
        "--fullscreen",
        "--fs-screen=" + std::to_string(video_display_),   // ★ 只用数字
        "--vo=gpu",
        "--volume=" + std::to_string(volume_),
        "--input-ipc-server=" + ipc_socket_,
        "--network-timeout=20",
        "--cache=yes",
        "--cache-secs=10",
        "--stream-lavf-o=reconnect=1",
        "--user-agent=Mozilla/5.0",
        "--log-file=" + mpv_log_file_,
    };

    if (!sub_file.empty()) {
        args.push_back("--sub-file=" + sub_file);
    }
    args.push_back(url);

    std::fprintf(stderr, "[Player] mpv play: %s (vol=%d)\n",
                 url.c_str(), volume_);

    pid_ = fork();
    if (pid_ < 0) {
        status_ = "failed";
        fail_reason_ = "fork failed";
        return;
    }
    if (pid_ == 0) {
        std::vector<char*> argv;
        for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);

        FILE* fp = freopen("/dev/null", "w", stdout);
        (void)fp;

        execvp("mpv", argv.data());
        _exit(127);
    }

    usleep(1500 * 1000);
    int st = 0;
    pid_t r = waitpid(pid_, &st, WNOHANG);
    if (r == pid_) {
        status_ = "failed";
        fail_reason_ = "mpv exited immediately";
        std::fprintf(stderr, "[Player] mpv exited immediately\n");
        pid_ = -1;
        return;
    }
    status_ = "playing";
}

void TVPlayer::stop() {
    if (pid_ > 0) {
        ::kill(pid_, SIGTERM);
        for (int i = 0; i < 20; i++) {
            int st = 0;
            pid_t r = waitpid(pid_, &st, WNOHANG);
            if (r == pid_ || r < 0) break;
            usleep(50 * 1000);
        }
        ::kill(pid_, SIGKILL);
        waitpid(pid_, nullptr, 0);
        pid_ = -1;
        status_ = "idle";
    } else {
        // ★ mpv 已退出，但状态可能还停在 failed
        if (status_ == "failed") status_ = "idle";
    }
    ::unlink(ipc_socket_.c_str());
}

bool TVPlayer::is_alive() {
    if (pid_ <= 0) return false;
    int st = 0;
    pid_t r = waitpid(pid_, &st, WNOHANG);
    if (r == pid_) {
        pid_ = -1;
        if (status_ == "playing") status_ = "failed";
        return false;
    }
    if (r < 0) { pid_ = -1; return false; }
    return true;
}

} // namespace tv