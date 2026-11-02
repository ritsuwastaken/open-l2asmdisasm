#ifndef VALUE_H
#define VALUE_H

#include <cstdint>
#include <l2asmdisasm.h>
#include <string>
#include <string_view>
#include <utility>

namespace value
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

std::string format(const Value &value, l2asmdisasm::FieldType type, const l2asmdisasm::TextOptions &options);
Value parse(std::string_view text, l2asmdisasm::FieldType type, const l2asmdisasm::TextOptions &options);
bool is_nan(std::uint32_t bits);
} // namespace value

#endif // VALUE_H
