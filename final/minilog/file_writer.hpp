#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace minilog {

class FileWriter {
public:
    explicit FileWriter(const std::string& path);
    ~FileWriter() noexcept;

    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;

    [[nodiscard]] bool append(std::string_view data) noexcept;
    [[nodiscard]] bool sync() noexcept;

    [[nodiscard]] std::uint64_t bytes_written() const noexcept {
        return bytes_written_;
    }

    [[nodiscard]] int last_error() const noexcept {
        return last_error_;
    }

private:
    int fd_{-1};
    std::uint64_t bytes_written_{0};
    int last_error_{0};
};

}  // namespace minilog

