#ifndef BINARY_H
#define BINARY_H

#include <algorithm>
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
    FieldType type;
    const char *name;
    unsigned size;
    unsigned width;
    bool is_signed = false;
};

inline constexpr TypeInfo types[] = {
    {FieldType::U32, "u32", 4, 32},
    {FieldType::I32, "i32", 4, 32, true},
    {FieldType::U16, "u16", 2, 16},
    {FieldType::I16, "i16", 2, 16, true},
    {FieldType::U8, "u8", 1, 8},
    {FieldType::I8, "i8", 1, 8, true},
    {FieldType::F32, "f32", 4, 32},
    {FieldType::UTF16, "utf16", 0, 0},
    {FieldType::STRING, "string", 0, 0},
    {FieldType::COMPACT, "compact", 0, 32, true},
    {FieldType::PADDING, "padding", 0, 8},
    {FieldType::I64, "i64", 8, 64, true},
    {FieldType::F64, "f64", 8, 64},
    {FieldType::GROUP, "group", 0, 0},
};
static_assert(std::size(types) == static_cast<std::size_t>(FieldType::GROUP) + 1);
static_assert(std::ranges::all_of(types, [](const TypeInfo &info)
                                  { return &info == &types[static_cast<std::size_t>(info.type)]; }),
              "types must follow FieldType order");

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
