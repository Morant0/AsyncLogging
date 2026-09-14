#include "minilog/fixed_buffer.hpp"
#include "test_support.hpp"

#include <string_view>

int main() {
    minilog::FixedBuffer<8> buffer;

    TEST_REQUIRE(buffer.empty());
    TEST_REQUIRE(buffer.available() == 8);
    TEST_REQUIRE(buffer.records() == 0);

    TEST_REQUIRE(buffer.append("1234567"));
    TEST_REQUIRE(buffer.size() == 7);
    TEST_REQUIRE(buffer.records() == 1);

    TEST_REQUIRE(buffer.append("8"));
    TEST_REQUIRE(buffer.size() == 8);
    TEST_REQUIRE(buffer.available() == 0);
    TEST_REQUIRE(buffer.records() == 2);

    const std::string_view before(buffer.data(), buffer.size());
    TEST_REQUIRE(before == "12345678");

    TEST_REQUIRE(!buffer.append("9"));
    TEST_REQUIRE(buffer.size() == 8);
    TEST_REQUIRE(buffer.records() == 2);

    buffer.reset();
    TEST_REQUIRE(buffer.empty());
    TEST_REQUIRE(buffer.available() == 8);
    TEST_REQUIRE(buffer.records() == 0);
    TEST_REQUIRE(buffer.append("again"));
}
