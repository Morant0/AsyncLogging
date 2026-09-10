# AsyncLogging

一个从零实现 C++17 异步日志库的渐进式学习项目。

当前完成的是 **Stage 0：同步日志写入**。这一阶段暂时不引入线程和队列，目标是先保证最底层的文件打开、完整写入、错误处理和资源释放是正确的。后续阶段将在此基础上逐步加入工作线程、有界队列、批量写入、固定缓冲区和双缓冲。

## 当前目录

```text
AsyncLogging/
├── CMakeLists.txt
├── README.md
├── .gitignore
└── stages/
    └── stage0_sync.cpp
```

## Stage 0 要解决什么问题

第一版采用同步调用链：

```mermaid
flowchart LR
    A[main] -->|log message| B[SyncLogger]
    B -->|message| C[FileWriter::append]
    B -->|newline| C
    C -->|write| D[stage0.log]
```

调用 `SyncLogger::log()` 的线程会亲自完成文件写入：

```text
业务线程
  └── SyncLogger::log(message)
        ├── append(message)
        └── append("\n")
              └── write(fd, ...)
```

它还不是异步日志库，但为后面的异步化提供了可靠的文件写入地基。

## 核心组件

### `FileWriter`

`FileWriter` 负责管理一个 POSIX 文件描述符：

```cpp
fd_ = ::open(
    path.c_str(),
    O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,
    0644
);
```

各标志的含义：

| 标志 | 作用 |
|---|---|
| `O_WRONLY` | 只写方式打开文件 |
| `O_CREAT` | 文件不存在时创建文件 |
| `O_APPEND` | 每次写入都追加到文件末尾 |
| `O_CLOEXEC` | 执行 `exec` 后自动关闭该文件描述符 |
| `0644` | 文件基础权限；最终权限还会受到 `umask` 影响 |

构造函数打开文件，析构函数关闭文件：

```text
构造 FileWriter → 获得 fd
FileWriter 存活  → 使用 fd 写入
析构 FileWriter → close(fd)
```

这就是 RAII：把文件描述符的生命周期绑定到 C++ 对象的生命周期。

`FileWriter` 禁止复制：

```cpp
FileWriter(const FileWriter&) = delete;
FileWriter& operator=(const FileWriter&) = delete;
```

否则两个对象可能同时认为自己拥有同一个文件描述符，并在析构时重复关闭它。

### 完整处理 `write()`

一次 `write()` 成功不代表请求的全部字节都已经写完。`append()` 使用循环处理短写：

```text
准备写入 remaining 个字节
          │
          ▼
    write(fd, current, remaining)
          │
          ├── written > 0
          │      更新 current 和 remaining，继续写剩余数据
          │
          ├── written == -1 且 errno == EINTR
          │      系统调用被信号中断，重新尝试
          │
          └── 其他情况
                 保存错误码并返回 false
```

`bytes_written_` 记录本对象已经成功交给内核的字节数，`last_error_` 保存最近一次失败的错误码。

### `sync()`

`FileWriter::sync()` 调用 `fsync()`，可以请求内核把该文件的已修改数据同步到底层存储设备：

```cpp
bool sync() noexcept;
```

需要区分：

```text
append() 成功
    表示数据已经成功交给内核，不一定已经抵达物理存储设备

sync() 成功
    表示完成了一次更强的持久化同步请求，但代价通常更高
```

当前 `main()` 只演示普通同步写入，并没有调用 `sync()`；接口是为后续显式刷新语义预留的。

### `SyncLogger`

`SyncLogger` 组合一个 `FileWriter`，并保证每条日志以换行结束：

```cpp
bool log(std::string_view message) noexcept {
    if (!writer_.append(message)) {
        return false;
    }

    return writer_.append("\n");
}
```

这里使用组合而不是继承，因为 `SyncLogger` 不是一种 `FileWriter`，而是“拥有并使用一个 `FileWriter`”。

## 构建要求

- CMake 3.16 或更高版本
- 支持 C++17 的 Clang 或 GCC
- macOS 或 Linux 等提供 POSIX 文件接口的系统

代码使用了 `open`、`write`、`fsync`、`close` 和 `unlink`，因此当前版本不能直接按原样在 Windows 上编译。

## 编译

在项目根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target stage0_sync -j
```

第一条命令读取项目根目录的 `CMakeLists.txt`，并在 `build/` 中生成构建文件；第二条命令编译 `stage0_sync` 目标。

## 运行

```bash
cd build
./stage0_sync
cat stage0.log
```

预期终端输出：

```text
wrote stage0.log
first synchronous log
同步版本先保证文件写入正确
```

其中第一行由程序输出，后两行是 `cat stage0.log` 显示的文件内容。

程序中的文件名是相对路径：

```cpp
SyncLogger logger("stage0.log");
```

因此日志会生成在程序启动时的当前工作目录。按照上面的命令运行时，它位于：

```text
AsyncLogging/build/stage0.log
```

## 为什么启动时调用 `unlink()`

```cpp
::unlink("stage0.log");
```

`FileWriter` 使用 `O_APPEND`，如果保留旧日志，反复运行程序就会不断追加内容。这里先删除同名测试日志，是为了让每次教学验证都稳定地产生两行结果。

这只是教学程序的测试行为。真实日志服务通常不能在启动时无条件删除历史日志。

## 当前阶段的边界

Stage 0 已经实现：

- 文件描述符的 RAII 管理；
- 禁止复制独占资源；
- 处理 `write()` 短写；
- 遇到 `EINTR` 时重试；
- 保存最后一次 I/O 错误；
- 提供 `fsync()` 封装；
- 为每条日志追加换行。

Stage 0 尚未实现：

- 后台线程；
- 线程安全保证；
- 有界阻塞队列和背压；
- 批量写入；
- 日志级别、时间和线程 ID；
- 文件滚动；
- 自动化测试；
- 对 `main()` 中两次 `log()` 返回值的检查。

## 下一阶段

Stage 1 将先实现一个单工作线程执行器，理解任务所有权和 Drain 关闭：

```text
调用线程提交任务
        ↓
有界或无界任务队列
        ↓
工作线程取出任务
        ↓
FileWriter 写入文件
```

这一阶段会开始使用：

```cpp
std::thread
std::mutex
std::condition_variable
std::queue
```

届时再在 `CMakeLists.txt` 中增加 `stage1_mini_pool` 目标和线程库依赖，不提前声明尚未创建的源文件。
