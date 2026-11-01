#ifndef BINARY_H
#define BINARY_H

#include "encoding.h"
#include "value.h"
#include <cstdint>
#include <l2asmdisasm.h>
#include <span>
#include <vector>

namespace binary
{
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
value::Value read(Reader &reader, l2asmdisasm::FieldType type, std::size_t size, const encoding::Conversion &conversion);
void write(std::vector<std::uint8_t> &output, l2asmdisasm::FieldType type, const value::Value &value, std::size_t size,
           const encoding::Conversion &conversion);
} // namespace binary

#endif // BINARY_H
