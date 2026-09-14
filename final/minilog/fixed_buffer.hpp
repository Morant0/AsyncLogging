#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace minilog {

template <std::size_t Capacity>
class FixedBuffer {
    static_assert(Capacity > 0, "buffer capacity must be positive");

public:
    [[nodiscard]] bool append(std::string_view text) noexcept {
        if (text.size() > available()) {
            return false;
        }

        if (!text.empty()) {
            std::memcpy(data_.data() + size_, text.data(), text.size());
        }
        size_ += text.size();
        ++records_;
        return true;
    }

    [[nodiscard]] const char* data() const noexcept {
        return data_.data();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] std::size_t records() const noexcept {
        return records_;
    }

    [[nodiscard]] std::size_t available() const noexcept {
        return Capacity - size_;
    }

    [[nodiscard]] bool empty() const noexcept {
        return size_ == 0;
    }

    void reset() noexcept {
        // 不清零整个数组；下次 append 会覆盖有效区域。
        size_ = 0;
        records_ = 0;
    }

private:
    std::array<char, Capacity> data_{};
    std::size_t size_{0};
    std::size_t records_{0};
};

}  // namespace minilog
