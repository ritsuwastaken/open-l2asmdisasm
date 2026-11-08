#include "encoding.h"
#include "util.h"

namespace encoding
{
namespace
{
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

std::uint32_t codepoint(std::string_view text, std::size_t &offset)
{
    const auto first = static_cast<std::uint8_t>(text[offset++]);
    if (first < 0x80) return first;
    const unsigned count = first >= 0xc2 && first <= 0xdf ? 1 : first >= 0xe0 && first <= 0xef ? 2
                                                            : first >= 0xf0 && first <= 0xf4   ? 3
                                                                                               : 0;
    if (count == 0 || count > text.size() - offset) util::fail("Invalid UTF-8");
    std::uint32_t code = first & (0x7f >> (count + 1));
    for (unsigned i = 0; i < count; ++i)
    {
        const auto next = static_cast<std::uint8_t>(text[offset++]);
        if ((next & 0xc0) != 0x80) util::fail("Invalid UTF-8 continuation");
        code = (code << 6) | (next & 0x3f);
    }
    if (code < (count == 1 ? 0x80u : count == 2 ? 0x800u
                                                : 0x10000u) ||
        code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) util::fail("Invalid UTF-8 code point");
    return code;
}

void put(std::vector<std::uint8_t> &bytes, std::uint32_t unit, bool wide)
{
    bytes.push_back(static_cast<std::uint8_t>(unit));
    if (wide) bytes.push_back(static_cast<std::uint8_t>(unit >> 8));
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
    if (wide && bytes.size() % 2 != 0) util::fail("Odd UTF-16LE byte length");
    if (!wide)
    {
        for (const auto byte : bytes)
            utf8(text, byte);
        return text;
    }
    const auto unit = [&](std::size_t i)
    {
        return static_cast<std::uint32_t>(bytes[i] | bytes[i + 1] << 8);
    };
    for (std::size_t i = 0; i < bytes.size(); i += 2)
    {
        auto code = unit(i);
        if (code >= 0xd800 && code <= 0xdbff)
        {
            i += 2;
            const auto low = i < bytes.size() ? unit(i) : 0;
            if (low < 0xdc00 || low > 0xdfff) util::fail("Invalid UTF-16 surrogate pair");
            code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
        }
        else if (code >= 0xdc00 && code <= 0xdfff) util::fail("Unpaired UTF-16 surrogate");
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
        else if (code < 0x10000) put(bytes, code, true);
        else
        {
            code -= 0x10000;
            put(bytes, 0xd800 | (code >> 10), true);
            put(bytes, 0xdc00 | (code & 0x3ff), true);
        }
    }
    return bytes;
}
} // namespace encoding
