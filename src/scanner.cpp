#include "scanner.hpp"
#include "config.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <map>
#include <cstdlib>
#include <limits.h>

namespace tv {

std::string TVScanner::APP_PATH_;

// ---------- 工具 ----------
static bool file_exists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

static bool is_dir(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static long long file_mtime(const std::string& p) {
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return 0;
    return (long long)st.st_mtime;
}

static std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static bool ends_with(const std::string& s, const std::string& suf) {
    if (s.size() < suf.size()) return false;
    return std::equal(suf.rbegin(), suf.rend(), s.rbegin());
}

// 从字符串里取 attr="..." 的内容，找不到返回空串
static std::string extract_attr(const std::string& line, const std::string& key) {
    std::string pat = key + "=\"";
    auto p = line.find(pat);
    if (p == std::string::npos) return "";
    p += pat.size();
    auto e = line.find('"', p);
    if (e == std::string::npos) return "";
    return line.substr(p, e - p);
}

// ---------- 1. find_sources ----------
std::vector<Source> TVScanner::find_sources() {
    if (APP_PATH_.empty()) APP_PATH_ = tv::APP_PATH;

    std::vector<std::string> bases = {
        "/roms/TV",
        "/mnt/mmc/TV",
        "/mnt/sdcard/TV",
    };
    if (!APP_PATH_.empty()) bases.push_back(APP_PATH_ + "/TV");

    std::vector<Source> out;
    std::set<std::string> seen;

    for (auto& base : bases) {
        if (!is_dir(base)) continue;
        DIR* d = opendir(base.c_str());
        if (!d) continue;

        struct dirent* ent;
        while ((ent = readdir(d)) != nullptr) {
            std::string fname = ent->d_name;
            if (fname.empty() || fname[0] == '.') continue;

            if (!ends_with(fname, ".m3u") &&
                !ends_with(fname, ".m3u8") &&
                !ends_with(fname, ".txt")) continue;

            std::string full = base + "/" + fname;

            // realpath 去重
            char real[PATH_MAX];
            if (realpath(full.c_str(), real)) {
                if (seen.count(real)) continue;
                seen.insert(real);
            }

            std::string name = fname;
            auto dot = name.find_last_of('.');
            if (dot != std::string::npos) name = name.substr(0, dot);

            out.push_back({full, name});
            std::fprintf(stderr, "[Scanner] found source: %s\n", name.c_str());
        }
        closedir(d);
    }

    // 按名字排序，让 L1/R1 顺序稳定
    std::sort(out.begin(), out.end(),
              [](const Source& a, const Source& b){ return a.name < b.name; });
    return out;
}

// ---------- 2. parse_m3u ----------
std::vector<Channel> TVScanner::parse_m3u(const std::string& filepath) {
    std::vector<Channel> out;
    std::ifstream f(filepath);
    if (!f.good()) return out;

    std::string line;
    std::string cur_name;
    std::string cur_group = "未分组";
    std::string cur_logo;

    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty()) continue;

        if (line[0] == '#') {
            if (line.rfind("#EXTINF", 0) == 0) {
                std::string g = extract_attr(line, "group-title");
                if (!g.empty()) cur_group = g;

                std::string lg = extract_attr(line, "tvg-logo");
                cur_logo = lg;

                std::string tn = extract_attr(line, "tvg-name");
                if (!tn.empty()) {
                    cur_name = tn;
                } else {
                    auto comma = line.find(',');
                    if (comma != std::string::npos)
                        cur_name = trim(line.substr(comma + 1));
                }
            }
            continue;
        }

        if (line.rfind("http", 0) == 0 && !cur_name.empty()) {
            Channel c;
            c.name       = cur_name;
            c.url        = line;
            c.group      = cur_group;
            c.logo       = cur_logo;
            c.line_no    = 1;
            c.line_total = 1;
            out.push_back(c);
            cur_name.clear();
            cur_logo.clear();
        }
    }

    // 统计同名频道的线路
    std::map<std::string, int> total;
    for (auto& c : out) total[c.name]++;

    std::map<std::string, int> seen_cnt;
    for (auto& c : out) {
        int n = ++seen_cnt[c.name];
        c.line_no    = n;
        c.line_total = total[c.name];
    }

    std::fprintf(stderr, "[Scanner] parsed %s: %zu channels\n",
                 filepath.c_str(), out.size());
    return out;
}

// ---------- 3. 缓存 ----------
std::string TVScanner::cache_path(const Source& src) {
    if (APP_PATH_.empty()) APP_PATH_ = tv::APP_PATH;

    std::string safe = src.name;
    for (auto& ch : safe) {
        if (ch == ' ') ch = '_';
        if (ch == '/') ch = '_';
    }
    return APP_PATH_ + "/.cache_" + safe + ".dat";
}

bool TVScanner::save_cache(const Source& src, const std::vector<Channel>& ch) {
    std::string p = cache_path(src);
    std::ofstream f(p);
    if (!f.good()) {
        std::fprintf(stderr, "[Scanner] save cache failed: %s\n", p.c_str());
        return false;
    }
    f << src.file << "\n";
    f << file_mtime(src.file) << "\n";
    f << ch.size() << "\n";
    for (auto& c : ch) {
        f << c.name << '\t'
          << c.url  << '\t'
          << c.group << '\t'
          << c.line_no << '\t'
          << c.line_total << '\t'
          << c.logo << "\n";
    }
    std::fprintf(stderr, "[Scanner] cache saved: %s (%zu channels)\n",
                 src.name.c_str(), ch.size());
    return true;
}

bool TVScanner::load_cache(const Source& src, std::vector<Channel>& out) {
    std::string p = cache_path(src);
    if (!file_exists(p)) return false;

    std::ifstream f(p);
    if (!f.good()) return false;

    std::string cached_path;
    long long   cached_mtime = 0;
    size_t      count = 0;

    if (!std::getline(f, cached_path)) return false;
    {
        std::string l;
        if (!std::getline(f, l)) return false;
        // 兼容 Python 版存的浮点字符串（如 "1758888888.123"）
        // 和 C++ 版的整数秒（如 "1758888888"）
        cached_mtime = (long long)std::atoll(l.c_str());
    }
    {
        std::string l;
        if (!std::getline(f, l)) return false;
        count = (size_t)std::atoll(l.c_str());
    }

    // 校验
    if (cached_path != src.file) return false;
    long long cur_mtime = file_mtime(src.file);
    long long diff = cur_mtime - cached_mtime;
    if (diff < 0) diff = -diff;
    if (diff > 1) return false;         // 容差 1 秒

    out.clear();
    out.reserve(count);

    std::string line;
    while (std::getline(f, line) && out.size() < count) {
        if (line.empty()) continue;
        // 按 \t 分割
        std::vector<std::string> parts;
        size_t start = 0;
        while (true) {
            auto tab = line.find('\t', start);
            if (tab == std::string::npos) {
                parts.push_back(line.substr(start));
                break;
            }
            parts.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }
        if (parts.size() < 5) continue;

        Channel c;
        c.name       = parts[0];
        c.url        = parts[1];
        c.group      = parts[2];
        c.line_no    = std::atoi(parts[3].c_str());
        c.line_total = std::atoi(parts[4].c_str());
        c.logo       = (parts.size() > 5) ? parts[5] : "";
        out.push_back(c);
    }

    if (out.size() != count) {
        std::fprintf(stderr, "[Scanner] cache count mismatch %zu/%zu, ignoring\n",
                     out.size(), count);
        out.clear();
        return false;
    }

    std::fprintf(stderr, "[Scanner] cache hit: %s (%zu channels)\n",
                 src.name.c_str(), out.size());
    return true;
}

// ---------- 4. 一步到位 ----------
std::vector<Channel> TVScanner::scan_source(const Source& src) {
    std::vector<Channel> ch;
    if (load_cache(src, ch)) return ch;
    ch = parse_m3u(src.file);
    if (!ch.empty()) save_cache(src, ch);
    return ch;
}

} // namespace tv