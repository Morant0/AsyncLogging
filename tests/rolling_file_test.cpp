#include "minilog/rolling_file.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;

int main() {
    // 创建独立的临时目录
    const auto unique =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path directory = fs::temp_directory_path() /
        ("minilog-roll-test-" + std::to_string(::getpid()) + '-' +
         std::to_string(unique));
    fs::create_directories(directory);

    const std::string first = "123456";
    const std::string second = "abcdef";
    const std::string oversized(20, 'X');
    const std::string final = "z";

    //写入数据
    {
        minilog::RollingFile output(
            (directory / "rolling").string(),
            10);
        TEST_REQUIRE(output.append(first));
        TEST_REQUIRE(output.append(second));
        TEST_REQUIRE(output.append(oversized));
        TEST_REQUIRE(output.append(final));
    }

    // 读取滚动产生的文件，把“文件名、文件内容”一起存入 files
    std::vector<std::pair<std::string, std::string>> files;
    for (const auto& entry : fs::directory_iterator(directory)) {
        TEST_REQUIRE(entry.is_regular_file());
        std::ifstream input(entry.path(), std::ios::binary);
        const std::string content(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());
        TEST_REQUIRE(!content.empty());
        files.emplace_back(entry.path().filename().string(), content);
    }

    // 检查文件数量与内容顺序
    TEST_REQUIRE(files.size() == 4);
    std::sort(files.begin(), files.end());

    std::string combined;
    for (const auto& file : files) {
        combined += file.second;
    }
    TEST_REQUIRE(combined == first + second + oversized + final);

    fs::remove_all(directory);
}
