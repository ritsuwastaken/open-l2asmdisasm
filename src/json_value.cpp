#include "encoding.h"
#include "json.h"
#include <algorithm>
#include <memory>

namespace l2asmdisasm::json
{
Json::Value make_object(std::initializer_list<std::pair<std::string, Json::Value>> members)
{
    Json::Value result(Json::objectValue);
    for (const auto &[name, value] : members)
        result[name] = value;
    return result;
}

Json::Value array() { return Json::Value(Json::arrayValue); }

const Json::Value &at(const Json::Value &value, const std::string &key)
{
    if (!value.isObject() || !value.isMember(key)) util::fail("Missing JSON property: " + key);
    return value[key];
}

const Json::Value &at(const Json::Value &value, std::size_t index)
{
    if (!value.isArray() || index >= value.size()) util::fail("JSON array index out of range");
    return value[static_cast<Json::ArrayIndex>(index)];
}

std::string string(const Json::Value &value)
{
    if (!value.isString()) util::fail("Expected JSON string");
    return value.asString();
}

bool boolean(const Json::Value &value)
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

namespace
{
void validate_tokens(std::string_view text, std::size_t &i)
{
    const auto digit = [](char c)
    {
        return c >= '0' && c <= '9';
    };
    const auto hex_unit = [&](std::size_t &pos)
    {
        if (text.size() - pos < 4) util::fail("Truncated JSON Unicode escape");
        const auto unit = util::number<std::uint16_t>(text.substr(pos, 4), 16);
        pos += 4;
        return unit;
    };
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
        }
    }
}
} // namespace

Json::Value parse_json(std::string_view text)
{
    if (text.empty()) util::fail("Empty JSON input");
    encoding::validate_utf8(text);
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    std::size_t offset = 0;
    try
    {
        validate_tokens(text, offset);
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
    Json::Value result;
    std::string errors;
    if (!reader->parse(text.data(), text.data() + text.size(), &result, &errors)) util::fail("JSON: " + errors);
    return result;
}

void object(const Json::Value &value, std::initializer_list<std::string_view> allowed)
{
    if (!value.isObject()) util::fail("Expected JSON object");
    for (const auto &key : value.getMemberNames())
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) util::fail("Unknown JSON property: " + key);
}

std::int64_t integer(const Json::Value &value)
{
    if ((value.type() != Json::intValue && value.type() != Json::uintValue) || !value.isInt64())
        util::fail("Expected signed 64-bit JSON integer");
    return static_cast<std::int64_t>(value.asInt64());
}

std::size_t count(const Json::Value &value, std::size_t minimum)
{
    const auto n = integer(value);
    if (n < static_cast<std::int64_t>(minimum) || n > static_cast<std::int64_t>(max_values)) util::fail("Count must be in [" + std::to_string(minimum) + ", 100000]");
    return static_cast<std::size_t>(n);
}
} // namespace l2asmdisasm::json
