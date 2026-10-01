#include "logo_loader.hpp"
#include "config.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <curl/curl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <sys/stat.h>
#include <thread>

namespace tv {

// FNV-1a 64bit 哈希，作为磁盘文件名（不追求抗碰撞）
static std::string hash_url(const std::string& url) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char c : url) { h ^= c; h *= 0x100000001b3ULL; }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
}

static size_t curl_write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* v = static_cast<std::vector<uint8_t>*>(userdata);
    size_t n = size * nmemb;
    v->insert(v->end(), (uint8_t*)ptr, (uint8_t*)ptr + n);
    return n;
}

// 把 path/query 里的非 ASCII 字符和空格百分号编码（libcurl 其实能处理，
// 但有些镜像会拒，这里保险一点）
static std::string normalize_url(const std::string& url) {
    auto p = url.find("://");
    if (p == std::string::npos) return url;
    auto ps = url.find('/', p + 3);
    if (ps == std::string::npos) return url;

    std::ostringstream oss;
    oss << url.substr(0, ps);
    for (unsigned char c : url.substr(ps)) {
        if (c >= 0x80 || c == ' ') {
            char b[8]; std::snprintf(b, sizeof(b), "%%%02X", c); oss << b;
        } else oss << (char)c;
    }
    return oss.str();
}

static bool file_exists(const std::string& p) {
    struct stat st; return stat(p.c_str(), &st) == 0;
}

LogoLoader::LogoLoader(const std::string& cache_dir) : cache_dir_(cache_dir) {
    mkdir(cache_dir_.c_str(), 0755);
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

LogoLoader::~LogoLoader() {
    for (auto& kv : raw_cache_)    if (kv.second) SDL_FreeSurface(kv.second);
    for (auto& kv : scaled_cache_) if (kv.second) SDL_FreeSurface(kv.second);
    curl_global_cleanup();
}

std::string LogoLoader::disk_path(const std::string& url) const {
    return cache_dir_ + "/" + hash_url(url) + ".png";
}

SDL_Surface* LogoLoader::load_from_disk(const std::string& url) {
    std::string p = disk_path(url);
    if (!file_exists(p)) return nullptr;
    SDL_Surface* s = IMG_Load(p.c_str());
    if (!s) return nullptr;
    SDL_Surface* conv = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(s);
    return conv;
}

void LogoLoader::start_fetch(const std::string& url) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (loading_.count(url) || failed_.count(url)) return;
        if (raw_cache_.count(url)) return;
        loading_.insert(url);
    }
    std::thread([this, url]() { fetch_thread(url); }).detach();
}

void LogoLoader::fetch_thread(std::string url) {
    std::vector<uint8_t> buf;
    long http_code = 0;

    CURL* curl = curl_easy_init();
    if (curl) {
        std::string real = normalize_url(url);
        curl_easy_setopt(curl, CURLOPT_URL, real.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");

        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        // 自动解压 gzip / deflate
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
        CURLcode rc = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(curl);

        if (rc != CURLE_OK || buf.empty() || http_code >= 400) {
            std::fprintf(stderr, "[Logo] fetch failed: %s (rc=%d http=%ld)\n",
                         url.c_str(), (int)rc, http_code);
            buf.clear();
        }
    }

    std::lock_guard<std::mutex> lk(mtx_);
    loading_.erase(url);
    if (buf.empty()) failed_.insert(url);
    else             ready_buffers_[url] = std::move(buf);
}

void LogoLoader::process_pending() {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> jobs;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& kv : ready_buffers_) jobs.emplace_back(std::move(kv));
        ready_buffers_.clear();
    }

    for (auto& job : jobs) {
        const std::string& url = job.first;

        SDL_RWops* rw = SDL_RWFromMem(job.second.data(), (int)job.second.size());
        SDL_Surface* s = IMG_Load_RW(rw, 1);
        if (!s) {
            std::fprintf(stderr, "[Logo] decode failed: %s\n", url.c_str());
            std::lock_guard<std::mutex> lk(mtx_);
            failed_.insert(url);
            continue;
        }
        SDL_Surface* conv = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(s);
        if (!conv) {
            std::lock_guard<std::mutex> lk(mtx_);
            failed_.insert(url);
            continue;
        }

        IMG_SavePNG(conv, disk_path(url).c_str());

        std::lock_guard<std::mutex> lk(mtx_);
        raw_cache_[url] = conv;
        std::fprintf(stderr, "[Logo] loaded %dx%d: %s\n",
                     conv->w, conv->h, url.c_str());
    }
}

SDL_Surface* LogoLoader::get(const std::string& url, int max_w, int max_h) {
    if (url.empty()) return nullptr;

    char keybuf[512];
    std::snprintf(keybuf, sizeof(keybuf), "%s|%dx%d", url.c_str(), max_w, max_h);
    std::string key = keybuf;

    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (failed_.count(url)) return nullptr;
        auto it = scaled_cache_.find(key);
        if (it != scaled_cache_.end()) return it->second;
    }

    SDL_Surface* raw = nullptr;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = raw_cache_.find(url);
        if (it != raw_cache_.end()) raw = it->second;
    }
    if (!raw) {
        SDL_Surface* disk = load_from_disk(url);
        if (disk) {
            std::lock_guard<std::mutex> lk(mtx_);
            raw_cache_[url] = disk;
            raw = disk;
        }
    }
    if (!raw) {
        start_fetch(url);
        return nullptr;
    }

    float scale = std::min((float)max_w / raw->w, (float)max_h / raw->h);
    int tw = std::max(1, (int)(raw->w * scale));
    int th = std::max(1, (int)(raw->h * scale));

    SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(
        0, tw, th, 32, SDL_PIXELFORMAT_RGBA32);
    if (!scaled) return nullptr;
    SDL_BlitScaled(raw, nullptr, scaled, nullptr);

    {
        std::lock_guard<std::mutex> lk(mtx_);
        scaled_cache_[key] = scaled;
    }
    return scaled;
}

bool LogoLoader::is_loading(const std::string& url) const {
    std::lock_guard<std::mutex> lk(mtx_);
    return loading_.count(url) != 0;
}
bool LogoLoader::is_failed(const std::string& url) const {
    std::lock_guard<std::mutex> lk(mtx_);
    return failed_.count(url) != 0;
}

} // namespace tv