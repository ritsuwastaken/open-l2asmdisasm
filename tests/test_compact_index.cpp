#include "support.h"

#include <limits>
#include <random>
#include <string>

using namespace l2asmdisasm;

namespace
{
const auto single = parse_schema(R"({"safePackage":false,"blocks":[{"recordCount":1,"fields":[{"name":"c","type":"compact"}]}]})");
const auto followed = parse_schema(R"({"safePackage":false,"blocks":[{"recordCount":1,"fields":[{"name":"c","type":"compact"},{"name":"next","type":"u8"}]}]})");
const auto many = parse_schema(R"({"safePackage":false,"blocks":[{"recordCount":"eof","fields":[{"name":"c","type":"compact"}]}]})");

std::string record(std::int64_t value)
{
    return "[{\"c\":" + std::to_string(value) + "}]";
}
} // namespace

TEST(CompactIndex, KnownValues)
{
    const std::pair<std::int32_t, std::vector<std::uint8_t>> cases[] = {
        {0, {0x00}},
        {1, {0x01}},
        {-1, {0x81}},
        {63, {0x3f}},
        {-63, {0xbf}},
        {64, {0x40, 0x01}},
        {-64, {0xc0, 0x01}},
        {8191, {0x7f, 0x7f}},
        {8192, {0x40, 0x80, 0x01}},
        {1048575, {0x7f, 0xff, 0x7f}},
        {1048576, {0x40, 0x80, 0x80, 0x01}},
        {134217727, {0x7f, 0xff, 0xff, 0x7f}},
        {134217728, {0x40, 0x80, 0x80, 0x80, 0x01}},
        {std::numeric_limits<std::int32_t>::max(), {0x7f, 0xff, 0xff, 0xff, 0x0f}},
        {std::numeric_limits<std::int32_t>::min(), {0xc0, 0x80, 0x80, 0x80, 0x10}},
    };
    for (const auto &[value, bytes] : cases)
    {
        EXPECT_EQ(assemble(record(value), single), bytes) << value;
        EXPECT_EQ(assemble(disassemble(bytes, single), single), bytes) << value;
        EXPECT_NE(disassemble(bytes, single).find("\"c\": " + std::to_string(value)), std::string::npos) << value;
    }
}

TEST(CompactIndex, RejectsMalformed)
{
    const std::vector<std::vector<std::uint8_t>> cases = {
        {},
        {0x40},
        {0x40, 0x80},
        {0x40, 0x80, 0x80},
        {0x40, 0x80, 0x80, 0x80},
        {0x40, 0x80, 0x80, 0x80, 0x10}, // positive overflow
        {0xc1, 0x80, 0x80, 0x80, 0x10}, // negative overflow
        {0x40, 0x80, 0x80, 0x80, 0x20}, // invalid fifth byte
        {0x40, 0x80, 0x80, 0x80, 0x80, 0x00},
    };
    for (const auto &bytes : cases)
        EXPECT_THROW((void)disassemble(bytes, single), std::invalid_argument);
    EXPECT_THROW((void)assemble(record(std::int64_t{std::numeric_limits<std::int32_t>::max()} + 1), single), std::invalid_argument);
}

TEST(CompactIndex, StopsAtEnd)
{
    const std::vector<std::uint8_t> trailing = {0x40, 0x01, 0xff};
    const auto text = disassemble(trailing, followed);
    EXPECT_NE(text.find("\"c\": 64"), std::string::npos);
    EXPECT_NE(text.find("\"next\": 255"), std::string::npos);
    EXPECT_NE(disassemble(std::vector<std::uint8_t>{0xc0, 0x00}, single).find("\"c\": 0"), std::string::npos);
}

TEST(CompactIndex, RandomRoundTrip)
{
    std::mt19937 generator(0x4c32);
    std::uniform_int_distribution<std::int32_t> values(std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());
    std::string json = "[";
    for (int i = 0; i < 20000; ++i)
        json += (i ? ",{\"c\":" : "{\"c\":") + std::to_string(values(generator)) + "}";
    json += "]";
    const auto bytes = assemble(json, many);
    EXPECT_EQ(assemble(disassemble(bytes, many), many), bytes);
}
