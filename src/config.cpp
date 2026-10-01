#include "config.hpp"
#include <unistd.h>
#include <limits.h>
#include <cstdio>

namespace tv {

std::string APP_PATH;
std::string FONT_FILE;

void init_paths() {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        std::string p(buf);
        auto slash = p.find_last_of('/');
        APP_PATH = (slash == std::string::npos) ? "." : p.substr(0, slash);
    } else {
        APP_PATH = ".";
    }

    std::string local = APP_PATH + "/font/font.ttf";
    if (access(local.c_str(), R_OK) == 0) {
        FONT_FILE = local;
    } else {
        FONT_FILE = "/mnt/vendor/bin/default.ttf";
    }
    std::fprintf(stderr, "[config] APP_PATH=%s FONT=%s\n",
                 APP_PATH.c_str(), FONT_FILE.c_str());
}

} // namespace tv