#include "support.h"

#include <array>
#include <limits>
#include <random>

using namespace l2asmdisasm;

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
        EXPECT_EQ(encode_compact_index(value), bytes) << value;
        const auto decoded = decode_compact_index(bytes);
        EXPECT_EQ(decoded.value, value);
        EXPECT_EQ(decoded.bytes_read, bytes.size());
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
        EXPECT_THROW((void)decode_compact_index(bytes), std::invalid_argument);
}

TEST(CompactIndex, StopsAtEnd)
{
    const std::array<std::uint8_t, 3> trailing = {0x40, 0x01, 0xff};
    const auto decoded = decode_compact_index(trailing);
    EXPECT_EQ(decoded.value, 64);
    EXPECT_EQ(decoded.bytes_read, 2u);
    const std::array<std::uint8_t, 2> redundant = {0xc0, 0x00};
    EXPECT_EQ(decode_compact_index(redundant).value, 0);
}

TEST(CompactIndex, RandomRoundTrip)
{
    std::mt19937 generator(0x4c32);
    std::uniform_int_distribution<std::int32_t> values(std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());
    for (int i = 0; i < 100000; ++i)
    {
        const auto value = values(generator);
        const auto encoded = encode_compact_index(value);
        const auto result = decode_compact_index(encoded);
        ASSERT_EQ(result.value, value);
        ASSERT_EQ(result.bytes_read, encoded.size());
    }
}
