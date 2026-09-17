#include "minilog/file_writer.hpp"

#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace minilog {

FileWriter::FileWriter(const std::string& path)
    : fd_(::open(
          path.c_str(),
          O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,
          0644)) {
    if (fd_ == -1) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "failed to open log file: " + path);
    }
}

FileWriter::~FileWriter() noexcept {
    if (fd_ != -1) {
        // close() 失败已经无法在析构函数中可靠汇报；不能从析构函数抛异常。
        ::close(fd_);
    }
}

bool FileWriter::append(std::string_view data) noexcept {
    const char* current = data.data();
    std::size_t remaining = data.size();

    while (remaining > 0) {
        const ssize_t written = ::write(fd_, current, remaining);

        if (written > 0) {
            const auto count = static_cast<std::size_t>(written);
            current += count;
            remaining -= count;
            bytes_written_ += static_cast<std::uint64_t>(count);
            continue;
        }

        if (written == -1 && errno == EINTR) {
            continue;
        }

        last_error_ = written == 0 ? EIO : errno;
        return false;
    }

    return true;
}

bool FileWriter::sync() noexcept {
    while (::fsync(fd_) == -1) {
        if (errno == EINTR) {
            continue;
        }
        last_error_ = errno;
        return false;
    }
    return true;
}

}  // namespace minilog

