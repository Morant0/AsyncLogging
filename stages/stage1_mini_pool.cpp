#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <fcntl.h>
#include <unistd.h>
#include <thread>

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

    bool append(std::string_view data) noexcept { // Append data to the file
        const char* current = data.data();
        std::size_t remaining = data.size();

        while(remaining > 0) {
            const ssize_t written = ::write(fd_, current, remaining);  // returns the number of bytes written, or -1 on error
            
            if (written > 0) {
                const auto count = static_cast<std::size_t>(written);
                current += count;
                remaining -= count;
            } else if (written == -1 && errno == EINTR) {
                continue; // Interrupted by signal, retry
            } else {
                return false; // Write failed
            }
        }

        return true;
    }

    bool sync() noexcept { // Synchronize the file to disk
        while (::fsync(fd_) == -1) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry
            }

            return false; // Sync failed
        }

        return true;
    }

private:
    int fd_{-1}; // File descriptor for the file being written to
};

// 这一版故意先借用“单工作线程的迷你线程池”理解异步边界。
class MiniThreadPool {

public:
    MiniThreadPool() : worker_([this] { this->run(); }) {}

    ~MiniThreadPool() { 
        shutdown(); 
    }

    bool submit(std::function<void()> task) {
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(stopping_) return false;
            tasks_.push_back(std::move(task));
        }

        ready_.notify_all();

        return true;
    }

    void shutdown() {
        std::lock_guard<std::mutex> lifecycle_mutex(lifecycle_mutex_);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }

        ready_.notify_all();

        if (worker_.joinable())
            worker_.join();
    }

private:

    void run() noexcept {
        while(true) {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [this] { 
                    return stopping_ || !tasks_.empty();
                });
            

                if (stopping_ && tasks_.empty()) return;

                task = std::move(tasks_.front());
                tasks_.pop_front();
            }

            try {
                task();
            } catch(...) {
                static constexpr char error[] = "log task failed\n";
                ::write(STDERR_FILENO, error, sizeof(error) - 1);
            }
        }
    }

    std::deque<std::function<void()>> tasks_; // Queue of tasks to be executed
    std::thread worker_; // Worker thread that executes tasks
    std::mutex mutex_; // Mutex for synchronizing access to the task queue
    std::mutex lifecycle_mutex_; // Mutex for synchronizing the lifecycle of the thread pool
    bool stopping_{false}; // Flag to indicate whether the thread pool should stop
    std::condition_variable ready_; // Condition variable to signal when tasks are ready
};

class LoggerViaPool {

public:
    explicit LoggerViaPool(const char* path) : writer_(path) {}

    bool log(std::string message) {
        message.push_back('\n');
        return pool_.submit([this, owned = std::move(message)] {
            writer_.append(owned);
        });
    }

    void shutdown() {
        pool_.shutdown();
    }

private:
    // 析构顺序与声明顺序相反：pool_ 必须先析构并 join，writer_ 才能关闭。
    FileWriter writer_;
    MiniThreadPool pool_;
};

int main() {
    try {
        ::unlink("stage1_pool.log");
        LoggerViaPool logger("stage1_pool.log");
        for (int i = 0; i < 1000; ++i) {
            logger.log("task " + std::to_string(i));
        }
        logger.shutdown();
        std::cout << "wrote 1000 lines to stage1_pool.log\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}


