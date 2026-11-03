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

constexpr bool is_integer(FieldType type) { return type <= FieldType::CHAR || type == FieldType::CNTR; }
constexpr bool is_hexadecimal(FieldType type) { return type == FieldType::HEX || type == FieldType::CHEX; }

inline std::size_t min_size(FieldType type)
{
    switch (type)
    {
    case FieldType::UCHAR:
    case FieldType::CHEX:
    case FieldType::CHAR:
    case FieldType::ASCF:
    case FieldType::CNTR:
    case FieldType::FILLER:
        return 1;
    case FieldType::UWORD:
    case FieldType::WORD:
        return 2;
    case FieldType::UINT:
    case FieldType::HEX:
    case FieldType::INT:
    case FieldType::FLOAT:
    case FieldType::UNICODE:
        return 4;
    case FieldType::MAT:
        return 4;
    case FieldType::INT64:
    case FieldType::DOUBLE:
    case FieldType::MTX:
    case FieldType::MTX2:
    case FieldType::MAT2:
        return 8;
    case FieldType::MTX3:
        return 12;
    case FieldType::GROUP:
        return 0;
    }
    fail("Invalid field type");
}
} // namespace util

#endif // UTIL_H
