#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <condition_variable>
#include <cerrno>
#include <unistd.h>
#include <deque>
#include <optional>
#include <fcntl.h>

class FileWriter {
public:
    explicit FileWriter(const char* path)
        : fd_(::open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644)) {
        if (fd_ == -1) {
            throw std::runtime_error(
                std::string("open failed: ") + std::strerror(errno));
        }
    }

    ~FileWriter() {
        if (fd_ != -1) {
            ::close(fd_);
        }
    }

    bool append(std::string_view text) {
        const char* data = text.data();
        std::size_t remaining = text.size();
        while (remaining > 0) {
            const ssize_t written = ::write(fd_, data, remaining);
            if (written > 0) {
                const auto count = static_cast<std::size_t>(written);
                data += count;
                remaining -= count;
            } else if (written == -1 && errno == EINTR) {
                continue;
            } else {
                return false;
            }
        }
        return true;
    }

private:
    int fd_{-1};
};


template <class T>
class BoundedBlockingQueue {

public:
    explicit BoundedBlockingQueue(std::size_t capacity) : capacity_(capacity) {
        if (capacity == 0)
            throw std::invalid_argument("queue capacity must be positive");
    }

    bool push(T value) {
        std::unique_lock<std::mutex> lock(mutex_);

        not_full_.wait(lock, [this] {
            return closed_ || queue_.size() < capacity_;
        });

        if(closed_) return false;

        queue_.push_back(value);
        not_empty_.notify_one();

        return true;
    }

    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);

        not_empty_.wait(lock, [this] {
            return closed_ || !queue_.empty();
        });

        if (queue_.empty()) return std::nullopt;

        T value = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();

        return value;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_full_.notify_all();
        not_empty_.notify_all();
    }

private:
    const std::size_t capacity_;
    std::deque<T> queue_;
    bool closed_{false};
    std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
};

class AsyncLogger {

public: 
    AsyncLogger(const char* path, std::size_t queue_capacity) 
        : writer_(path), queue_(queue_capacity),  worker_([this] { run(); }) {}

    ~AsyncLogger() {
        shutdown();
    }

    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;

    bool log(std::string message) {
        message.push_back('\n');
        return queue_.push(std::move(message));
    }

    void shutdown() {
        std::lock_guard<std::mutex> lifecycle_mutex(lifecycle_mutex_);
        queue_.close();
        if (worker_.joinable()) worker_.join();
    }

private:
    void run() noexcept {
        while (auto message = queue_.pop()) {
            if (!writer_.append(*message)) {
                static constexpr char error[] = "async logger write failed\n";
                ::write(STDERR_FILENO, error, sizeof(error) - 1);
            }
        }
    }

    FileWriter writer_;
    BoundedBlockingQueue<std::string> queue_;
    std::thread worker_;
    std::mutex lifecycle_mutex_;
};

int main() {
    try {
        ::unlink("stage2.log");
        AsyncLogger logger("stage2.log", 1024);

        std::thread first([&logger] {
            for (int i = 0; i < 1000; ++i) {
                logger.log("first producer: " + std::to_string(i));
            }
        });

        std::thread second([&logger] {
            for (int i = 0; i < 1000; ++i) {
                logger.log("second producer: " + std::to_string(i));
            }
        });

        first.join();
        second.join();
        logger.shutdown();

        std::cout << "wrote 2000 lines to stage2.log\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}