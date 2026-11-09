#ifndef BINARY_H
#define BINARY_H

#include <cstdint>
#include <iterator>
#include <l2asmdisasm.h>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace l2asmdisasm::binary
{
struct TypeInfo
{
    const char *name;
    unsigned size;
    unsigned width;
    bool is_signed = false;
};

inline constexpr TypeInfo types[] = {
    {"u32", 4, 32},
    {"i32", 4, 32, true},
    {"u16", 2, 16},
    {"i16", 2, 16, true},
    {"u8", 1, 8},
    {"i8", 1, 8, true},
    {"f32", 4, 32},
    {"utf16", 0, 0},
    {"string", 0, 0},
    {"compact", 0, 32, true},
    {"padding", 0, 8},
    {"i64", 8, 64, true},
    {"f64", 8, 64},
    {"group", 0, 0},
};
static_assert(std::size(types) == static_cast<std::size_t>(FieldType::GROUP) + 1);

inline const TypeInfo &type_info(FieldType type)
{
    const auto index = static_cast<std::size_t>(type);
    if (index >= std::size(types)) throw std::invalid_argument("Invalid field type");
    return types[index];
}

constexpr bool is_integer(FieldType type) { return type <= FieldType::I8 || type == FieldType::COMPACT; }

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
