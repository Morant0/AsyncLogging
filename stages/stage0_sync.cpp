#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>

class FileWriter {

public:
    explicit FileWriter(const std::string& path) 
        : fd_{::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644)} {
        if (fd_ == -1) {
            throw std::runtime_error("Failed to open file: " + path + ", error: " + std::to_string(errno));
        }
    }

    ~FileWriter() {
        if (fd_ != -1) {
            ::close(fd_);
        }
    };

    FileWriter(const FileWriter&) = delete; // Disable copy constructor
    FileWriter& operator=(const FileWriter&) = delete; // Disable copy assignment

    bool append(std::string_view data) noexcept; // Append data to the file

    bool sync() noexcept; // Synchronize the file to disk

    std::uint64_t bytes_written() const noexcept { 
        return bytes_written_; 
    } // Get total bytes written

    int last_error() const noexcept {
        return last_error_; 
    } // Get the last error code

private:
    int fd_{-1}; // File descriptor for the file being written to
    std::uint64_t bytes_written_{0};  // Total number of bytes written to the file
    int last_error_{0};  // Last error code encountered during file operations

};

bool FileWriter::append(std::string_view data) noexcept {
    const char* current = data.data();
    std::size_t remaining = data.size();

    while(remaining > 0) {
        ssize_t written = ::write(fd_, current, remaining);  // returns the number of bytes written, or -1 on error
        
        if (written > 0) {
            const auto count = static_cast<std::size_t>(written);
            bytes_written_ += count;
            current += count;
            remaining -= count;
            continue;
        }

        if (written == -1 && errno == EINTR) {
            continue; // Interrupted by signal, retry
        }

        last_error_ = written == 0 ? EIO : errno; // If written is 0, treat it as an I/O error

        return false; 
    }

    return true;
}

bool FileWriter::sync() noexcept {
    while (::fsync(fd_) == -1) {
        if (errno == EINTR) {
            continue; // Interrupted by signal, retry
        }
        last_error_ = errno;
        return false; // Sync failed
    }

    return true;
}

class SyncLogger {

public:
    explicit SyncLogger(const std::string& path) : writer_(path) {}

    bool log(std::string_view message) noexcept {
        if(!writer_.append(message))
            return false;

        return writer_.append("\n"); // Append a newline after the message
    }



private:
    FileWriter writer_; // FileWriter instance for writing logs
};

int main() {
    try {
        ::unlink("stage0.log");
        SyncLogger logger("stage0.log");
        logger.log("first synchronous log");
        logger.log("同步版本先保证文件写入正确");
        std::cout << "wrote stage0.log\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
