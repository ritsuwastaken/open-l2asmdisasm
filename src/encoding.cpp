#include "encoding.h"
#include "util.h"

namespace l2asmdisasm::encoding
{
namespace
{
constexpr std::uint32_t high_surrogate = 0xd800, low_surrogate = 0xdc00, surrogate_end = 0xe000, supplementary = 0x10000, max_code_point = 0x10ffff;
constexpr unsigned surrogate_bits = 10;
constexpr std::uint32_t surrogate_payload = (1u << surrogate_bits) - 1;

bool is_high(std::uint32_t unit) { return unit >= high_surrogate && unit < low_surrogate; }
bool is_low(std::uint32_t unit) { return unit >= low_surrogate && unit < surrogate_end; }

void utf8(std::string &output, std::uint32_t code)
{
    if (code < 0x80) output += static_cast<char>(code);
    else if (code < 0x800)
    {
        output += static_cast<char>(0xc0 | (code >> 6));
        output += static_cast<char>(0x80 | (code & 0x3f));
    }
    else if (code < 0x10000)
    {
        output += static_cast<char>(0xe0 | (code >> 12));
        output += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
        output += static_cast<char>(0x80 | (code & 0x3f));
    }
    else
    {
        output += static_cast<char>(0xf0 | (code >> 18));
        output += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
        output += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
        output += static_cast<char>(0x80 | (code & 0x3f));
    }
}

unsigned trailing_bytes(std::uint8_t first)
{
    if (first >= 0xc2 && first < 0xe0) return 1;
    if (first >= 0xe0 && first < 0xf0) return 2;
    if (first >= 0xf0 && first < 0xf5) return 3;
    return 0;
}

std::uint32_t codepoint(std::string_view text, std::size_t &offset)
{
    constexpr std::uint32_t minimum[] = {0, 0x80, 0x800, 0x10000};
    const auto first = static_cast<std::uint8_t>(text[offset++]);
    if (first < 0x80) return first;
    const auto count = trailing_bytes(first);
    if (count == 0 || count > text.size() - offset) util::fail("Invalid UTF-8");
    std::uint32_t code = first & (0x7f >> (count + 1));
    for (unsigned i = 0; i < count; ++i)
    {
        const auto next = static_cast<std::uint8_t>(text[offset++]);
        if ((next & 0xc0) != 0x80) util::fail("Invalid UTF-8 continuation");
        code = (code << 6) | (next & 0x3f);
    }
    if (code < minimum[count] || code > max_code_point || (code >= high_surrogate && code < surrogate_end)) util::fail("Invalid UTF-8 code point");
    return code;
}

void put16(std::vector<std::uint8_t> &bytes, std::uint32_t unit)
{
    bytes.push_back(static_cast<std::uint8_t>(unit));
    bytes.push_back(static_cast<std::uint8_t>(unit >> 8));
}
} // namespace

void validate_utf8(std::string_view text)
{
    std::size_t offset = 0;
    while (offset < text.size())
        (void)codepoint(text, offset);
}

std::string decode(std::span<const std::uint8_t> bytes, bool wide)
{
    std::string text;
    if (!wide)
    {
        for (const auto byte : bytes)
            utf8(text, byte);
        return text;
    }
    if (bytes.size() % 2 != 0) util::fail("Odd UTF-16LE byte length");
    const auto unit = [&](std::size_t i)
    {
        return static_cast<std::uint32_t>(bytes[i] | bytes[i + 1] << 8);
    };
    for (std::size_t i = 0; i < bytes.size(); i += 2)
    {
        auto code = unit(i);
        if (is_high(code))
        {
            i += 2;
            const auto low = i < bytes.size() ? unit(i) : 0;
            if (!is_low(low)) util::fail("Invalid UTF-16 surrogate pair");
            code = supplementary + ((code - high_surrogate) << surrogate_bits) + (low - low_surrogate);
        }
        else if (is_low(code)) util::fail("Unpaired UTF-16 surrogate");
        utf8(text, code);
    }
    return text;
}

std::vector<std::uint8_t> encode(std::string_view text, bool wide)
{
    std::vector<std::uint8_t> bytes;
    std::size_t offset = 0;
    while (offset < text.size())
    {
        auto code = codepoint(text, offset);
        if (!wide)
        {
            if (code > 0xff) util::fail("Character cannot be represented as ISO-8859-1 string");
            bytes.push_back(static_cast<std::uint8_t>(code));
        }
        else if (code < supplementary) put16(bytes, code);
        else
        {
            code -= supplementary;
            put16(bytes, high_surrogate | (code >> surrogate_bits));
            put16(bytes, low_surrogate | (code & surrogate_payload));
        }
    }
    return bytes;
}
} // namespace l2asmdisasm::encoding
