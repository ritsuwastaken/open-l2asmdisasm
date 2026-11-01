#include <l2asmdisasm.h>

#include <array>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

void check(std::int32_t value, std::vector<std::uint8_t> bytes)
{
    require(l2asmdisasm::encode_compact_index(value) == bytes, "Known encoding mismatch");
    const auto decoded = l2asmdisasm::decode_compact_index(bytes);
    require(decoded.value == value && decoded.bytes_read == bytes.size(), "Known decoding mismatch");
}

void reject(std::vector<std::uint8_t> bytes)
{
    try
    {
        (void)l2asmdisasm::decode_compact_index(bytes);
    }
    catch (const std::invalid_argument &)
    {
        return;
    }
    throw std::runtime_error("Malformed input accepted");
}
} // namespace

int main()
{
    try
    {
        check(0, {0x00});
        check(1, {0x01});
        check(-1, {0x81});
        check(63, {0x3f});
        check(-63, {0xbf});
        check(64, {0x40, 0x01});
        check(-64, {0xc0, 0x01});
        check(8191, {0x7f, 0x7f});
        check(8192, {0x40, 0x80, 0x01});
        check(1048575, {0x7f, 0xff, 0x7f});
        check(1048576, {0x40, 0x80, 0x80, 0x01});
        check(134217727, {0x7f, 0xff, 0xff, 0x7f});
        check(134217728, {0x40, 0x80, 0x80, 0x80, 0x01});
        check(std::numeric_limits<std::int32_t>::max(), {0x7f, 0xff, 0xff, 0xff, 0x0f});
        check(std::numeric_limits<std::int32_t>::min(), {0xc0, 0x80, 0x80, 0x80, 0x10});

        reject({});
        reject({0x40});
        reject({0x40, 0x80});
        reject({0x40, 0x80, 0x80});
        reject({0x40, 0x80, 0x80, 0x80});
        reject({0x40, 0x80, 0x80, 0x80, 0x10}); // positive overflow
        reject({0xc1, 0x80, 0x80, 0x80, 0x10}); // negative overflow
        reject({0x40, 0x80, 0x80, 0x80, 0x20}); // invalid fifth byte
        reject({0x40, 0x80, 0x80, 0x80, 0x80, 0x00});

        const std::array<std::uint8_t, 3> trailing = {0x40, 0x01, 0xff};
        const auto decoded = l2asmdisasm::decode_compact_index(trailing);
        require(decoded.value == 64 && decoded.bytes_read == 2, "Trailing bytes consumed");
        const std::array<std::uint8_t, 2> redundant = {0xc0, 0x00};
        require(l2asmdisasm::decode_compact_index(redundant).value == 0, "Redundant negative zero rejected");

        std::mt19937 generator(0x4c32);
        std::uniform_int_distribution<std::int32_t> values(
            std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());
        for (int i = 0; i < 100000; ++i)
        {
            const auto value = values(generator);
            const auto encoded = l2asmdisasm::encode_compact_index(value);
            const auto result = l2asmdisasm::decode_compact_index(encoded);
            require(result.value == value && result.bytes_read == encoded.size(), "Round-trip mismatch");
        }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
