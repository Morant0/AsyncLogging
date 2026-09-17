#include "minilog/rolling_file.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <new>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <unistd.h>

namespace minilog {

namespace {

std::uint64_t next_instance_id() noexcept {
    static std::atomic<std::uint64_t> identity{0};
    return identity.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

RollingFile::RollingFile(std::string base_name, std::uint64_t roll_size)
    : base_name_(std::move(base_name)),
      roll_size_(roll_size),
      instance_id_(next_instance_id()) {
    if (base_name_.empty()) {
        throw std::invalid_argument("log base name must not be empty");
    }
    if (roll_size_ == 0) {
        throw std::invalid_argument("roll size must be positive");
    }

    writer_ = std::make_unique<FileWriter>(make_filename());
    ++sequence_;
}

bool RollingFile::append(std::string_view data) noexcept {
    const std::uint64_t current_size = writer_->bytes_written();
    const auto incoming_size = static_cast<std::uint64_t>(data.size());

    const bool would_exceed =
        current_size > 0 &&
        (current_size >= roll_size_ ||
         incoming_size > roll_size_ - current_size);

    if (would_exceed && !roll()) {
        return false;
    }

    if (!writer_->append(data)) {
        last_error_ = writer_->last_error();
        return false;
    }

    return true;
}

bool RollingFile::sync() noexcept {
    if (!writer_->sync()) {
        last_error_ = writer_->last_error();
        return false;
    }
    return true;
}

bool RollingFile::roll() noexcept {
    try {
        // 先成功打开新文件，再替换旧 writer，保证失败时旧对象仍然有效。
        auto replacement = std::make_unique<FileWriter>(make_filename());
        writer_.swap(replacement);
        ++sequence_;
        return true;
    } catch (const std::system_error& error) {
        last_error_ = error.code().value();
    } catch (const std::bad_alloc&) {
        last_error_ = ENOMEM;
    } catch (...) {
        last_error_ = EFAULT;
    }
    return false;
}

std::string RollingFile::make_filename() const {
    const auto now = std::chrono::system_clock::now();
    const auto since_epoch = now.time_since_epoch();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(since_epoch);
    const auto fractional = micros.count() % 1000000;
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};
    if (::localtime_r(&seconds, &local_time) == nullptr) {
        throw std::runtime_error("localtime_r failed");
    }

    std::ostringstream name;
    name << base_name_ << '.'
         << std::put_time(&local_time, "%Y%m%d-%H%M%S") << '.'
         << std::setw(6) << std::setfill('0') << fractional << '.'
         << static_cast<long long>(::getpid()) << '.'
         << instance_id_ << '.'
         << sequence_ << ".log";
    return name.str();
}

}  // namespace minilog
