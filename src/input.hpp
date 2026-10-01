#pragma once
#include <string>

namespace tv {

class InputHandler {
public:
    InputHandler();
    ~InputHandler();

    bool ok() const { return fd_ >= 0; }

    // 主循环每帧调用一次：读一个事件并更新内部状态
    void poll();

    const std::string& code_name() const { return code_name_; }
    int  value() const { return value_; }

    bool is_key(const std::string& name) const {
        return code_name_ == name;
    }
    bool is_key(const std::string& name, int val) const {
        return code_name_ == name && value_ == val;
    }

    void reset() { code_name_.clear(); value_ = 0; }

    const std::string& device_path() const { return device_path_; }

private:
    std::string find_device() const;

    int         fd_ = -1;
    std::string device_path_;
    std::string code_name_;
    int         value_ = 0;
};

} // namespace tv