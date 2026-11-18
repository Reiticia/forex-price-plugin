#pragma once

#include <Windows.h>
#include <array>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace fxprice
{
// Readers pin one immutable snapshot per accessor and calling thread. ABI text
// pointers remain valid until that same accessor is called again on that thread
// (or the plugin is unloaded). A writer never changes a pinned string.
// Storage belongs to the plugin, not TLS: no TLS destructors into an unloaded DLL.
template<class T, std::size_t Accessors>
class PublishedSnapshot
{
public:
    PublishedSnapshot() : current_(std::make_shared<const T>()) {}
    void Publish(T value)
    {
        std::atomic_store(&current_, std::make_shared<const T>(std::move(value)));
    }
    std::shared_ptr<const T> Load() const { return std::atomic_load(&current_); }
    std::shared_ptr<const T> Pin(std::size_t accessor) const
    {
        auto snapshot = Load();
        std::lock_guard<std::mutex> lock(readersMutex_);
        readers_[GetCurrentThreadId()][accessor] = snapshot;
        return snapshot;
    }
private:
    std::shared_ptr<const T> current_;
    mutable std::mutex readersMutex_;
    mutable std::unordered_map<DWORD, std::array<std::shared_ptr<const T>, Accessors>> readers_;
};
} // namespace fxprice
