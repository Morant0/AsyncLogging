#pragma once

#include "minilog/file_writer.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace minilog {

class RollingFile {
public:
    RollingFile(std::string base_name, std::uint64_t roll_size);

    RollingFile(const RollingFile&) = delete;
    RollingFile& operator=(const RollingFile&) = delete;

    [[nodiscard]] bool append(std::string_view data) noexcept;
    [[nodiscard]] bool sync() noexcept;

    [[nodiscard]] int last_error() const noexcept {
        return last_error_;
    }

private:
    [[nodiscard]] bool roll() noexcept;
    [[nodiscard]] std::string make_filename() const;

    std::string base_name_;
    std::uint64_t roll_size_;
    std::uint64_t instance_id_{0};
    std::uint64_t sequence_{0};
    std::unique_ptr<FileWriter> writer_;
    int last_error_{0};
};

}  // namespace minilog
