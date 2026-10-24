#pragma once

#include <cstdint>
#include <atomic>
#include <string>
#include <vector>

namespace radio {
class RequestEpoch {
public:
    uint64_t Advance() { return ++value_; }
    bool IsCurrent(uint64_t ticket) const { return ticket == value_.load(); }
private:
    std::atomic<uint64_t> value_{0};
};

inline bool ReleasePower(bool owned, bool active, bool pending, bool idle) {
    return owned && !active && !pending && idle;
}

inline bool ValidUrl(const std::string& url) {
    const size_t prefix = url.starts_with("https://") ? 8 : url.starts_with("http://") ? 7 : 0;
    if (!prefix || url.size() <= prefix || url.size() > 2048) return false;
    auto host_end = url.find_first_of("/?#", prefix);
    auto host = url.substr(prefix, host_end - prefix);
    if (host.empty() || host.find('@') != std::string::npos) return false;
    for (unsigned char c : url) {
        if (c <= 32 || c == 127 || c == '\\') return false;
    }
    return url.find('#') == std::string::npos;
}

// ICY metadata can be fragmented across any number of HTTP reads.
class IcyFilter {
public:
    explicit IcyFilter(size_t interval) : interval_(interval), remaining_(interval) {}
    void Feed(const uint8_t* data, size_t size, std::vector<uint8_t>& audio) {
        audio.clear();
        for (size_t i = 0; i < size; ++i) {
            if (!interval_) {
                audio.push_back(data[i]);
            } else if (skip_) {
                --skip_;
                if (!skip_) remaining_ = interval_;
            } else if (!remaining_) {
                skip_ = static_cast<size_t>(data[i]) * 16;
                if (!skip_) remaining_ = interval_;
            } else {
                audio.push_back(data[i]);
                --remaining_;
            }
        }
    }
private:
    size_t interval_;
    size_t remaining_;
    size_t skip_ = 0;
};
}  // namespace radio
