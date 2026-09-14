#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <unistd.h>
#include <vector>


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
class BoundedBatchQueue {

public:
    explicit BoundedBatchQueue(const std::size_t capacity) : capacity_(capacity) {
        if (capacity == 0)
            throw std::invalid_argument("queue capacity must be positive");
    }

    bool push(T value) {
        std::unique_lock<std::mutex> lock(mutex_);

        not_full_.wait(lock, [this] {
            return closed_ || queue_.size() < capacity_;
        });

        if(closed_) return false;

        queue_.push_back(std::move(value));
        lock.unlock();
        not_empty_.notify_one();
        return true;
    }

    bool take_batch(std::vector<T>& output, std::size_t max_count) {
        if(max_count == 0) throw std::invalid_argument("max_count must be positive");

        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] {
            return closed_ || !queue_.empty();
        });

        if(queue_.empty()) return false;

        const std::size_t count = std::min(queue_.size(), max_count);
        output.clear();
        output.reserve(count);

        for(std::size_t i = 0; i < count; i ++) {
            output.push_back(std::move(queue_.front()));
            queue_.pop_front();
        }

        lock.unlock();
        not_full_.notify_all();
        return true;
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
    std::size_t capacity_;
    std::deque<T> queue_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    bool closed_{false};
    std::mutex mutex_;
};

class BatchLogger {

public:
    BatchLogger(const char* path, const std::size_t capacity) :
        writer_(path), queue_(capacity), worker_([this] { run(); }) {}

    ~BatchLogger() { 
        shutdown(); 
    }

    bool log(std::string message) {
        message.push_back('\n');
        return queue_.push(std::move(message));
    }

    void shutdown() {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
        queue_.close();
        if (worker_.joinable()) {
            worker_.join();
        }
    }


private:
    void run() noexcept {
        try {
            run_impl();
        } catch (...) {
            queue_.close();
            static constexpr char error[] = "batch logger worker failed\n";
            ::write(STDERR_FILENO, error, sizeof(error) - 1);
        }
        
    }

    void run_impl() {
        std::vector<std::string> messages;
        std::string batch;

        while(queue_.take_batch(messages, 256)) {
            std::size_t bytes = 0;
            for (const auto& message : messages) {
                bytes += message.size();
            }

            batch.clear();
            batch.reserve(bytes);
            for (const auto& message : messages) {
                batch.append(message);
            }

            if(!writer_.append(batch)) {
                static constexpr char error[] = "batch write failed\n";
                ::write(STDERR_FILENO, error, sizeof(error) - 1);
            }
        }
    }


    FileWriter writer_;
    BoundedBatchQueue<std::string> queue_;
    std::thread worker_;
    std::mutex lifecycle_mutex_;
};

int main() {
    try {
        ::unlink("stage3.log");
        BatchLogger logger("stage3.log", 4096);
        for (int i = 0; i < 10000; ++i) {
            logger.log("batched log: " + std::to_string(i));
        }
        logger.shutdown();
        std::cout << "wrote 10000 batched lines to stage3.log\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}


