#pragma once
#include <string>
#include <vector>

namespace tv {

struct Channel {
    std::string name;       // 频道名
    std::string url;        // 播放地址
    std::string group;      // 分组
    std::string logo;       // 台标 URL（可空）
    int         line_no    = 1;   // 同名频道的第几条线路
    int         line_total = 1;   // 同名频道的线路总数
};

struct Source {
    std::string file;       // m3u 文件的绝对路径
    std::string name;       // 文件名（去扩展名）
};

class TVScanner {
public:
    // 扫描目录，返回所有源
    static std::vector<Source> find_sources();

    // 解析 m3u
    static std::vector<Channel> parse_m3u(const std::string& filepath);

    // 缓存
    static std::string cache_path(const Source& src);
    static bool        save_cache(const Source& src, const std::vector<Channel>& ch);
    static bool        load_cache(const Source& src, std::vector<Channel>& out);

    // 一步到位：尽量读缓存，失效则重新解析并写缓存
    static std::vector<Channel> scan_source(const Source& src);

private:
    static std::string APP_PATH_;
};

} // namespace tv