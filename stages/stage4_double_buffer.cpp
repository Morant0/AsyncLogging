#include <array>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <unistd.h>

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

template <std::size_t Capacity>
class FixedBuffer {

public:
    bool append(std::string_view text) noexcept {
        if (text.size() > available()) return false;

        if (!text.empty())
            std::memcpy(data_.data() + size_, text.data(), text.size());
        
        size_ += text.size();
        records_++;
        return true;
    }


    std::size_t available() const noexcept {
        return Capacity - size_;
    }

    const char* data() const noexcept {
        return data_.data();
    }

    std::size_t size() const noexcept {
        return size_;
    }

    std::size_t records() const noexcept {
        return records_;
    }

    void reset() noexcept {
        size_ = 0;
        records_ = 0;
    }


private:
    std::array<char, Capacity> data_{};
    std::size_t size_{0};
    std::size_t records_{0};
};

class DoubleBufferLogger {

public:
    static constexpr std::size_t buffer_size = 64*1024;
    using Buffer = FixedBuffer<buffer_size>;
    using BufferPtr = std::unique_ptr<Buffer>;

    DoubleBufferLogger(
        const char* path,
        std::size_t max_pending_buffers,
        std::chrono::milliseconds flush_interval) : 
            writer_(path),
            max_pending_buffers_(validate_capacity(max_pending_buffers)),
            flush_interval_(validate_interval(flush_interval)),
            current_(std::make_unique<Buffer>()),
            next_(std::make_unique<Buffer>()),
            worker_([this] { run(); }) {}

    ~DoubleBufferLogger() {
        shutdown();
    }

    bool log(std::string message) {
        message.push_back('\n');
        if (message.size() > buffer_size) return false;

        std::unique_lock<std::mutex> lock(mutex_);
        
        if (!running_) return false;

        if (message.size() > current_->available()) {
            space_available_.wait(lock, [this] {
                return !running_ || pending_.size() < max_pending_buffers_;
            });

            if (!running_) return false;

            // 等待期间后台线程可能已把半满 current_ 换走，
            // 因此获得锁后必须重新检查容量。
            if (message.size() > current_->available()) {
                BufferPtr replacement = next_ ? std::move(next_) : std::make_unique<Buffer>();
                pending_.push_back(std::move(current_));
                current_ = std::move(replacement);
                work_available_.notify_one();
            }
        }

        return current_->append(message);
    }

    void shutdown() {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
        }

        work_available_.notify_all();
        space_available_.notify_all();

        if (worker_.joinable()) worker_.join();
    }

private:
    static std::size_t validate_capacity(std::size_t value) {
        if (value == 0) {
            throw std::invalid_argument("max pending buffers must be positive");
        }
        return value;
    }

    static std::chrono::milliseconds validate_interval(std::chrono::milliseconds value) {
        if (value.count() <= 0) {
            throw std::invalid_argument("flush interval must be positive");
        }
        return value;
    }

    void run() {
        try {
            run_impl();
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                running_ = false;
            }
            work_available_.notify_all();
            space_available_.notify_all();
            static constexpr char error[] = "double buffer worker failed\n";
            ::write(STDERR_FILENO, error, sizeof(error) - 1);
        }
    }

    void run_impl() {
        std::deque<BufferPtr> buffers_to_write;

        while (true) {
            // 锁内交换所有权，锁外执行 I/O
            {
                std::unique_lock<std::mutex> lock(mutex_);

                // 等待工作或定时刷新
                work_available_.wait_for(lock, flush_interval_, [this] {
                    return !running_ || !pending_.empty();  
                });

                if (current_->size() > 0) {
                    pending_.push_back(std::move(current_));
                    current_ = next_ ? std::move(next_) : std::make_unique<Buffer>();
                }

                pending_.swap(buffers_to_write);  // 交换两个 deque 的内部状态，通常是很便宜的操作
                space_available_.notify_all();

                if(!running_ && buffers_to_write.empty()) return;
            }

            for (const auto& buffer : buffers_to_write) {
                if (!writer_.append(std::string_view(buffer->data(), buffer->size()))) {
                    static constexpr char error[] = "double buffer write failed\n";
                    ::write(STDERR_FILENO, error, sizeof(error) - 1);
                }
            }

            // 只保留一个空闲缓冲区，防止流量洪峰后长期占用大量内存。
            if (!buffers_to_write.empty()) {
                BufferPtr recycled = std::move(buffers_to_write.back());
                buffers_to_write.pop_back();
                recycled->reset();

                std::lock_guard<std::mutex> lock(mutex_);
                if (!next_) {
                    next_ = std::move(recycled);
                }
            }

            buffers_to_write.clear();
        }
    }

    FileWriter writer_;
    const std::size_t max_pending_buffers_;
    const std::chrono::milliseconds flush_interval_;

    BufferPtr current_;             // 正在追加
    BufferPtr next_;                // 备用空 Buffer
    std::deque<BufferPtr> pending_; // 等待后台写入

    bool running_{true};
    std::mutex mutex_;
    std::condition_variable work_available_;
    std::condition_variable space_available_;
    std::thread worker_;
    std::mutex lifecycle_mutex_;
};

int main() {
    try {
        ::unlink("stage4.log");
        DoubleBufferLogger logger(
            "stage4.log",
            8,
            std::chrono::milliseconds(500));

        std::thread first([&logger] {
            for (int i = 0; i < 5000; ++i) {
                logger.log("producer A: " + std::to_string(i));
            }
        });
        std::thread second([&logger] {
            for (int i = 0; i < 5000; ++i) {
                logger.log("producer B: " + std::to_string(i));
            }
        });

        first.join();
        second.join();
        logger.shutdown();
        std::cout << "wrote 10000 lines to stage4.log\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
