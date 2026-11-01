#include "value.h"
#include "util.h"
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>

namespace value
{
using namespace l2asmdisasm;

namespace
{
std::string escape(std::string_view text)
{
    std::string result;
    for (const auto c : text)
    {
        switch (c)
        {
        case '\\':
            result += "\\\\";
            break;
        case '\0':
            result += "\\0";
            break;
        case '\t':
            result += "\\t";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\n':
            result += "\\n";
            break;
        default:
            result += c;
        }
    }
    return result;
}

std::string unescape(std::string_view text)
{
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        char c = text[i];
        if (c == '\\')
        {
            if (++i == text.size()) util::fail("Unterminated text escape");
            switch (text[i])
            {
            case '\\':
                c = '\\';
                break;
            case '0':
                c = '\0';
                break;
            case 't':
                c = '\t';
                break;
            case 'r':
                c = '\r';
                break;
            case 'n':
                c = '\n';
                break;
            default:
                util::fail("Unknown text escape");
            }
        }
        result += c;
    }
    return result;
}

std::string format_float(std::uint32_t bits, FloatFormat style)
{
    if (is_nan(bits)) return "NaN(" + util::hex(bits) + ")";
    const auto number = std::bit_cast<float>(bits);
    char buffer[128];
    const auto result = style == FloatFormat::Shortest || std::isinf(number)
                            ? std::to_chars(buffer, buffer + sizeof(buffer), number, std::chars_format::general, std::numeric_limits<float>::max_digits10)
                            : std::to_chars(buffer, buffer + sizeof(buffer), static_cast<double>(number),
                                            style == FloatFormat::Fixed ? std::chars_format::fixed : std::chars_format::scientific, 8);
    if (result.ec != std::errc{}) util::fail("Cannot format FLOAT");
    std::string text(buffer, result.ptr);
    if (style == FloatFormat::Scientific && !std::isinf(number))
        while (text.size() - text.find('e') < 5)
            text.insert(text.find('e') + 2, 1, '0');
    return text;
}

std::uint32_t parse_float(std::string_view text)
{
    if (text.starts_with("NaN(") && text.ends_with(')'))
    {
        const auto bits = util::number<std::uint32_t>(text.substr(4, text.size() - 5), 16);
        if (!is_nan(bits)) util::fail("Invalid NaN bit pattern");
        return bits;
    }
    float parsed{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) util::fail("Invalid FLOAT");
    return std::bit_cast<std::uint32_t>(parsed);
}
} // namespace

bool is_nan(std::uint32_t bits) { return (bits & 0x7f800000) == 0x7f800000 && (bits & 0x7fffff) != 0; }

std::string format(const Value &value, FieldType type, const TextOptions &options)
{
    switch (type)
    {
    case FieldType::HEX:
    case FieldType::CHEX:
        return util::hex(static_cast<std::uint32_t>(value.integer)).substr(2);
    case FieldType::FLOAT:
        return format_float(static_cast<std::uint32_t>(value.integer), options.float_format);
    case FieldType::UNICODE:
        return escape(value.text);
    case FieldType::ASCF:
        return (value.wide && !options.force_ascii ? "u," : "a,") + escape(value.text);
    default:
        return std::to_string(value.integer);
    }
}

Value parse(std::string_view text, FieldType type, const TextOptions &options)
{
    if (type == FieldType::UNICODE) return {0, unescape(text), true};
    if (type == FieldType::ASCF)
    {
        if (!text.starts_with("a,") && !text.starts_with("u,")) util::fail("ASCF requires an a, or u, prefix");
        return {0, unescape(text.substr(2)), text[0] == 'u' && !options.force_ascii};
    }
    if (text.empty()) text = "0";
    switch (type)
    {
    case FieldType::FLOAT:
        return {parse_float(text)};
    case FieldType::FILLER:
    {
        const auto byte = util::number<unsigned>(text);
        if (byte > 255) util::fail("FILLER byte overflow");
        return {byte};
    }
    case FieldType::CNTR:
        return {util::number<std::int32_t>(text)};
    case FieldType::INT:
    case FieldType::WORD:
    case FieldType::CHAR:
    {
        const auto parsed = util::number<std::int32_t>(text);
        if ((type == FieldType::CHAR && (parsed < -128 || parsed > 127)) ||
            (type == FieldType::WORD && (parsed < -32768 || parsed > 32767))) util::fail("Signed field overflow");
        return {parsed};
    }
    default:
    {
        const auto parsed = util::number<std::uint32_t>(text, util::is_hexadecimal(type) ? 16 : 10);
        if ((util::width(type) == 1 && parsed > 255) || (util::width(type) == 2 && parsed > 65535)) util::fail("Unsigned field overflow");
        return {parsed};
    }
    }
}
} // namespace value
