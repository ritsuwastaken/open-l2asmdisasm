#ifndef BINARY_H
#define BINARY_H

#include "encoding.h"
#include <cstdint>
#include <l2asmdisasm.h>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace binary
{
struct Value
{
    std::int64_t integer = 0;
    std::string text;
    bool wide = false;

    Value() = default;
    Value(std::int64_t number) : integer(number) {}
    Value(std::string string, bool is_wide) : text(std::move(string)), wide(is_wide) {}
};

struct Reader
{
    std::span<const std::uint8_t> bytes;
    std::size_t offset = 0;

    std::span<const std::uint8_t> take(std::size_t count);
    std::uint32_t integer(std::size_t size);
    std::int32_t compact();
};

void put(std::vector<std::uint8_t> &output, std::uint32_t value, std::size_t size);
l2asmdisasm::CompactIndex decode_compact_index(std::span<const std::uint8_t> input);
std::vector<std::uint8_t> encode_compact_index(std::int32_t value);
Value read(Reader &reader, l2asmdisasm::FieldType type, std::size_t size);
void write(std::vector<std::uint8_t> &output, l2asmdisasm::FieldType type, const Value &value, std::size_t size);
} // namespace binary

#endif // BINARY_H
