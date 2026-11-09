#ifndef BINARY_H
#define BINARY_H

#include <cstdint>
#include <l2asmdisasm.h>
#include <span>
#include <string>
#include <vector>

namespace l2asmdisasm::binary
{
struct Value
{
    std::int64_t integer = 0;
    std::string text;
    bool wide = false;
};

struct Reader
{
    std::span<const std::uint8_t> bytes;
    std::size_t offset = 0;

    std::span<const std::uint8_t> take(std::uint64_t count);
    std::uint64_t integer(std::size_t size);
    std::int32_t compact();
};

void put(std::vector<std::uint8_t> &output, std::uint64_t value, std::size_t size);
void put_compact(std::vector<std::uint8_t> &output, std::int32_t value);
Value read(Reader &reader, FieldType type, std::size_t size);
void write(std::vector<std::uint8_t> &output, FieldType type, const Value &value, std::size_t size);
} // namespace l2asmdisasm::binary

#endif // BINARY_H
