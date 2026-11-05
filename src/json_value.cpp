#include "encoding.h"
#include "json.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <memory>
#include <unordered_map>

namespace l2asmdisasm::json
{
JsonValue make_object(std::initializer_list<std::pair<std::string, JsonValue>> members)
{
    JsonValue result(Json::objectValue);
    for (const auto &[name, value] : members)
        result[name] = value;
    return result;
}

JsonValue array() { return JsonValue(Json::arrayValue); }

const JsonValue &at(const JsonValue &value, const std::string &key)
{
    if (!value.isObject() || !value.isMember(key)) util::fail("Missing JSON property: " + key);
    return value[key];
}

const JsonValue &at(const JsonValue &value, std::size_t index)
{
    if (!value.isArray() || index >= value.size()) util::fail("JSON array index out of range");
    return value[static_cast<Json::ArrayIndex>(index)];
}

std::string string(const JsonValue &value)
{
    if (!value.isString()) util::fail("Expected JSON string");
    return value.asString();
}

bool boolean(const JsonValue &value)
{
    if (!value.isBool()) util::fail("Expected JSON boolean");
    return value.asBool();
}

std::string source_location(std::string_view source, std::ptrdiff_t offset)
{
    if (source.empty()) return {};
    if (source.starts_with("\xEF\xBB\xBF")) source.remove_prefix(3);
    const auto end = std::min(static_cast<std::size_t>(std::max(offset, std::ptrdiff_t{0})), source.size());
    const auto prefix = source.substr(0, end);
    const auto line = 1 + std::count(prefix.begin(), prefix.end(), '\n');
    const auto newline = prefix.rfind('\n');
    const auto column = end - (newline == std::string_view::npos ? 0 : newline + 1) + 1;
    return " (line " + std::to_string(line) + ", column " + std::to_string(column) + ")";
}

using RealNumbers = std::unordered_map<std::ptrdiff_t, double>;
RealNumbers normalize_tokens(std::string &text, std::size_t &i)
{
    const auto digit = [](char c)
    {
        return c >= '0' && c <= '9';
    };
    const auto hex_unit = [&](std::size_t &i)
    {
        if (text.size() - i < 4) util::fail("Truncated JSON Unicode escape");
        const auto unit = util::number<std::uint16_t>(std::string_view(text).substr(i, 4), 16);
        i += 4;
        return unit;
    };
    RealNumbers numbers;
    while (i < text.size())
    {
        const auto c = text[i++];
        if (c == '"')
        {
            while (i < text.size())
            {
                const auto ch = text[i++];
                if (ch == '"')
                {
                    break;
                }
                if (ch != '\\') continue;
                if (i == text.size()) break;
                if (text[i++] != 'u') continue;
                const auto high = hex_unit(i);
                if (high >= 0xdc00 && high <= 0xdfff) util::fail("Unpaired JSON Unicode surrogate");
                if (high < 0xd800 || high > 0xdbff) continue;
                if (text.substr(i, 2) != "\\u") util::fail("Unpaired JSON Unicode surrogate");
                i += 2;
                const auto low = hex_unit(i);
                if (low < 0xdc00 || low > 0xdfff) util::fail("Invalid JSON Unicode surrogate pair");
            }
        }
        else if (c == '+') util::fail("Leading plus in JSON number");
        else if (c == '-' || digit(c))
        {
            --i;
            const auto start = i;
            if (text[i] == '-') ++i;
            if (i == text.size() || !digit(text[i])) util::fail("Invalid JSON number");
            if (text[i++] == '0')
            {
                if (i < text.size() && digit(text[i])) util::fail("Leading zero in JSON number");
            }
            else
                while (i < text.size() && digit(text[i]))
                    ++i;
            if (i < text.size() && text[i] == '.')
            {
                ++i;
                if (i == text.size() || !digit(text[i])) util::fail("Invalid JSON fraction");
                while (i < text.size() && digit(text[i]))
                    ++i;
            }
            if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
            {
                ++i;
                if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
                if (i == text.size() || !digit(text[i])) util::fail("Invalid JSON exponent");
                while (i < text.size() && digit(text[i]))
                    ++i;
            }
            const auto token = std::string_view(text).substr(start, i - start);
            if (token.find_first_of(".eE") != std::string_view::npos)
            {
                double number = 0;
                const auto result = std::from_chars(token.data(), token.data() + token.size(), number);
                if (result.ec != std::errc{} || result.ptr != token.data() + token.size() || !std::isfinite(number)) util::fail("JSON number out of range");
                numbers.emplace(static_cast<std::ptrdiff_t>(start), number);
                text[start] = '0';
                std::fill(text.begin() + start + 1, text.begin() + i, ' ');
            }
        }
    }
    return numbers;
}

void restore_numbers(JsonValue &value, const RealNumbers &numbers)
{
    if (const auto found = numbers.find(value.getOffsetStart()); found != numbers.end())
    {
        const auto start = value.getOffsetStart(), end = value.getOffsetLimit();
        value = found->second;
        value.setOffsetStart(start);
        value.setOffsetLimit(end);
    }
    else
        for (auto &child : value)
            restore_numbers(child, numbers);
}

JsonValue parse_json(std::string_view text)
{
    if (text.empty()) util::fail("Empty JSON input");
    encoding::validate_utf8(text);
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    std::string document(text);
    std::size_t offset = 0;
    RealNumbers numbers;
    try
    {
        numbers = normalize_tokens(document, offset);
    }
    catch (const std::invalid_argument &error)
    {
        util::fail(error.what() + source_location(text, static_cast<std::ptrdiff_t>(offset ? offset - 1 : 0)));
    }
    Json::CharReaderBuilder builder;
    Json::CharReaderBuilder::strictMode(&builder.settings_);
    builder["collectComments"] = false;
    builder["stackLimit"] = 256;
    builder["skipBom"] = false;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    JsonValue result;
    std::string errors;
    if (!reader->parse(document.data(), document.data() + document.size(), &result, &errors)) util::fail("JSON: " + errors);
    restore_numbers(result, numbers);
    return result;
}

void object(const JsonValue &value, std::initializer_list<std::string_view> allowed)
{
    if (!value.isObject()) util::fail("Expected JSON object");
    for (const auto &key : value.getMemberNames())
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) util::fail("Unknown JSON property: " + key);
}

std::int64_t integer(const JsonValue &value)
{
    if ((value.type() != Json::intValue && value.type() != Json::uintValue) || !value.isInt64())
        util::fail("Expected signed 64-bit JSON integer");
    return static_cast<std::int64_t>(value.asInt64());
}

std::size_t count(const JsonValue &value, std::size_t minimum)
{
    const auto n = integer(value);
    if (n < static_cast<std::int64_t>(minimum) || n > static_cast<std::int64_t>(limit)) util::fail("Count must be in [" + std::to_string(minimum) + ", 100000]");
    return static_cast<std::size_t>(n);
}
} // namespace l2asmdisasm::json
