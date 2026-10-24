#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "sdkconfig.h"

class AudioService;

// Process-lifetime player. A single worker owns all HTTP and decoder handles.
#if CONFIG_ENABLE_RADIO_PLAYER
class RadioPlayer {
public:
    explicit RadioPlayer(AudioService& audio) : audio_(audio) {}
    bool Play(const std::string& name, const std::string& url);
    void Stop();
    bool IsActive() const;
    std::string Status() const;
private:
    AudioService& audio_;
    mutable std::mutex mutex_;
    TaskHandle_t task_ = nullptr;
    std::atomic<uint32_t> generation_{0};
    std::string name_, url_, state_ = "stopped", error_;
    uint32_t audio_token_ = 0;
    void Worker();
    std::string Stream(uint32_t generation, uint32_t audio_token, const std::string& url);
};
#else
class RadioPlayer {
public:
    explicit RadioPlayer(AudioService&) {}
    bool Play(const std::string&, const std::string&) { return false; }
    void Stop() {}
    bool IsActive() const { return false; }
    std::string Status() const { return "{\"state\":\"disabled\"}"; }
};
#endif
