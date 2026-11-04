#ifndef UTIL_H
#define UTIL_H

#include <charconv>
#include <cstdint>
#include <iterator>
#include <l2asmdisasm.h>
#include <stdexcept>
#include <string>
#include <string_view>

namespace util
{
using l2asmdisasm::FieldType;

[[noreturn]] inline void fail(const std::string &message) { throw std::invalid_argument(message); }

template <typename T>
T number(std::string_view text, int base = 10)
{
    if (base == 16 && (text.starts_with("0x") || text.starts_with("0X"))) text.remove_prefix(2);
    T value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) fail("Invalid or out-of-range integer '" + std::string(text) + "'");
    return value;
}

inline std::string hex(std::uint64_t value)
{
    char buffer[16];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, 16);
    for (auto *c = buffer; c != result.ptr; ++c)
        if (*c >= 'a' && *c <= 'f') *c -= 'a' - 'A';
    return "0x" + std::string(buffer, result.ptr);
}

constexpr bool is_integer(FieldType type) { return type <= FieldType::I8 || type == FieldType::COMPACT; }

struct TypeInfo
{
    const char *name;
    unsigned size;
    bool signed_value = false;
};

constexpr TypeInfo types[] = {
    {"u32", 4}, {"i32", 4, true}, {"u16", 2}, {"i16", 2, true}, {"u8", 1}, {"i8", 1, true}, {"f32", 4}, {"utf16", 4}, {"string", 1}, {"compact", 1, true}, {"padding", 1}, {"i64", 8, true}, {"f64", 8}, {"group", 0}};

inline const TypeInfo &type_info(FieldType type)
{
    const auto index = static_cast<std::size_t>(type);
    if (index >= std::size(types)) fail("Invalid field type");
    return types[index];
}

inline std::size_t min_size(FieldType type) { return type_info(type).size; }
} // namespace util

#endif // UTIL_H
