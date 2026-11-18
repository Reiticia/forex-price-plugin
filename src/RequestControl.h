#pragma once

#include <atomic>
#include <chrono>
#include <string>

namespace fxprice
{
// One deadline for the whole refresh/validation, including all Pro symbols.
// Cancel only sets an atomic flag; it is safe during DLL static destruction.
class RequestControl
{
public:
    explicit RequestControl(std::chrono::milliseconds budget = std::chrono::seconds(20))
        : deadline_(std::chrono::steady_clock::now() + budget) {}
    void Cancel() { cancelled_.store(true); }
    bool Check(std::string& error) const
    {
        if (cancelled_.load()) { error = "行情请求已取消"; return false; }
        if (std::chrono::steady_clock::now() >= deadline_)
        { error = "行情请求超过整轮时间限制"; return false; }
        return true;
    }
private:
    std::atomic<bool> cancelled_{};
    const std::chrono::steady_clock::time_point deadline_;
};
} // namespace fxprice
