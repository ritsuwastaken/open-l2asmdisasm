#ifndef UTIL_H
#define UTIL_H

#include <charconv>
#include <cstdint>
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
constexpr bool is_hexadecimal(FieldType type) { return type == FieldType::HEX32 || type == FieldType::HEX8; }

inline std::size_t min_size(FieldType type)
{
    switch (type)
    {
    case FieldType::U8:
    case FieldType::HEX8:
    case FieldType::I8:
    case FieldType::STRING:
    case FieldType::COMPACT:
    case FieldType::PADDING:
        return 1;
    case FieldType::U16:
    case FieldType::I16:
        return 2;
    case FieldType::U32:
    case FieldType::HEX32:
    case FieldType::I32:
    case FieldType::F32:
    case FieldType::UTF16:
        return 4;
    case FieldType::I64:
    case FieldType::F64:
        return 8;
    case FieldType::GROUP:
        return 0;
    }
    fail("Invalid field type");
}
} // namespace util

#endif // UTIL_H
