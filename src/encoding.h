#ifndef ENCODING_H
#define ENCODING_H

#include <cstdint>
#include <l2asmdisasm.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace encoding
{
struct Charset
{
    std::string_view name;
    std::uint16_t high[128];
};

struct Conversion
{
    const l2asmdisasm::TextOptions &options;
    const Charset &table;

    explicit Conversion(const l2asmdisasm::TextOptions &text_options);
};

void validate_utf8(std::string_view text);
const Charset &charset(std::string_view name);
std::vector<std::string> charset_names();
std::string decode(std::span<const std::uint8_t> bytes, bool wide, const Conversion &conversion);
std::vector<std::uint8_t> encode(std::string_view text, bool wide, const Conversion &conversion);
} // namespace encoding

#endif // ENCODING_H
