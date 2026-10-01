#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <cstdint>

struct SDL_Surface;

namespace tv {

class LogoLoader {
public:
    LogoLoader(const std::string& cache_dir);
    ~LogoLoader();

    // 返回缩放到不超过 max_w × max_h 的台标 surface。
    // 返回的 surface 由 LogoLoader 持有，调用者不要释放。
    // 未就绪返回 nullptr，会自动触发后台下载。
    SDL_Surface* get(const std::string& url, int max_w, int max_h);

    // 主循环每帧调一次：把已下载完的字节解码为 surface
    void process_pending();

    bool is_loading(const std::string& url) const;
    bool is_failed (const std::string& url) const;

private:
    void         start_fetch(const std::string& url);
    void         fetch_thread(std::string url);
    std::string  disk_path(const std::string& url) const;
    SDL_Surface* load_from_disk(const std::string& url);

    std::string cache_dir_;

    std::unordered_map<std::string, SDL_Surface*> raw_cache_;      // url -> surface
    std::unordered_map<std::string, SDL_Surface*> scaled_cache_;   // "url|WxH" -> surface
    std::unordered_map<std::string, std::vector<uint8_t>> ready_buffers_;  // 下载完待解码

    mutable std::mutex             mtx_;
    std::unordered_set<std::string> loading_;
    std::unordered_set<std::string> failed_;
};

} // namespace tv