#include "encoding.h"
#include "json.h"
#include <algorithm>
#include <format>
#include <memory>

namespace l2asmdisasm::json
{
Json::Value array() { return Json::Value(Json::arrayValue); }

const Json::Value &at(const Json::Value &value, const std::string &key)
{
    if (!value.isObject() || !value.isMember(key)) throw std::invalid_argument(std::format("Missing JSON property: {}", key));
    return value[key];
}

std::string string(const Json::Value &value)
{
    if (!value.isString()) throw std::invalid_argument("Expected JSON string");
    return value.asString();
}

bool boolean(const Json::Value &value)
{
    if (!value.isBool()) throw std::invalid_argument("Expected JSON boolean");
    return value.asBool();
}

std::string source_location(std::string_view source, std::ptrdiff_t offset)
{
    if (source.empty()) return {};
    const auto end = std::min(static_cast<std::size_t>(std::max(offset, std::ptrdiff_t{0})), source.size());
    const auto prefix = source.substr(0, end);
    const auto line = 1 + std::count(prefix.begin(), prefix.end(), '\n');
    const auto newline = prefix.rfind('\n');
    const auto column = end - (newline == std::string_view::npos ? 0 : newline + 1) + 1;
    return std::format(" (line {}, column {})", line, column);
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
        if (text.size() - pos < 4) throw std::invalid_argument("Truncated JSON Unicode escape");
        std::uint16_t unit = 0;
        const auto *digits = text.data() + pos;
        const auto result = std::from_chars(digits, digits + 4, unit, 16);
        if (result.ec != std::errc{} || result.ptr != digits + 4) throw std::invalid_argument("Invalid JSON Unicode escape");
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
                if (high >= 0xdc00 && high <= 0xdfff) throw std::invalid_argument("Unpaired JSON Unicode surrogate");
                if (high < 0xd800 || high > 0xdbff) continue;
                if (text.substr(i, 2) != "\\u") throw std::invalid_argument("Unpaired JSON Unicode surrogate");
                i += 2;
                const auto low = hex_unit(i);
                if (low < 0xdc00 || low > 0xdfff) throw std::invalid_argument("Invalid JSON Unicode surrogate pair");
            }
        }
        else if (c == '+') throw std::invalid_argument("Leading plus in JSON number");
        else if (c == '-' || digit(c))
        {
            --i;
            if (text[i] == '-') ++i;
            if (i == text.size() || !digit(text[i])) throw std::invalid_argument("Invalid JSON number");
            if (text[i++] == '0')
            {
                if (i < text.size() && digit(text[i])) throw std::invalid_argument("Leading zero in JSON number");
            }
            else
                while (i < text.size() && digit(text[i]))
                    ++i;
            if (i < text.size() && text[i] == '.')
            {
                ++i;
                if (i == text.size() || !digit(text[i])) throw std::invalid_argument("Invalid JSON fraction");
                while (i < text.size() && digit(text[i]))
                    ++i;
            }
            if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
            {
                ++i;
                if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
                if (i == text.size() || !digit(text[i])) throw std::invalid_argument("Invalid JSON exponent");
                while (i < text.size() && digit(text[i]))
                    ++i;
            }
        }
    }
}

struct Skeleton
{
    std::string text;
    std::vector<std::pair<std::ptrdiff_t, std::ptrdiff_t>> removed;

    std::ptrdiff_t source(std::ptrdiff_t offset) const
    {
        const auto gap = std::ranges::upper_bound(removed, std::pair{offset, PTRDIFF_MAX});
        return offset + (gap == removed.begin() ? 0 : std::prev(gap)->second);
    }

    void relocate(Json::Value &value) const
    {
        value.setOffsetStart(source(value.getOffsetStart()));
        value.setOffsetLimit(source(value.getOffsetLimit()));
        for (auto &child : value)
            relocate(child);
    }
};

Skeleton skeleton(std::string_view text, std::size_t depth)
{
    Skeleton result;
    std::size_t level = 0, start = 0, copied = 0;
    bool quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const auto c = text[i];
        if (quoted && c == '\\') ++i;
        else if (c == '"') quoted = !quoted;
        else if (quoted) continue;
        else if (c == '{' || c == '[')
        {
            if (level++ == depth && c == '{') start = i + 1;
        }
        else if ((c == '}' || c == ']') && --level == depth && start > copied)
        {
            result.text += text.substr(copied, start - copied);
            copied = i;
            result.removed.emplace_back(result.text.size(), copied - result.text.size());
        }
    }
    result.text += text.substr(copied);
    return result;
}

std::unique_ptr<Json::CharReader> strict_reader()
{
    Json::CharReaderBuilder builder;
    Json::CharReaderBuilder::strictMode(&builder.settings_);
    builder["collectComments"] = false;
    builder["stackLimit"] = 256;
    builder["skipBom"] = false;
    return std::unique_ptr<Json::CharReader>(builder.newCharReader());
}

Json::Value parse(std::string_view text)
{
    thread_local const auto reader = strict_reader();
    Json::Value result;
    try
    {
        if (reader->parse(text.data(), text.data() + text.size(), &result, nullptr)) return result;
    }
    catch (const Json::Exception &error)
    {
        throw std::invalid_argument(std::format("JSON: {}", error.what()));
    }
    const auto error = reader->getStructuredErrors().front();
    throw LocatedError(std::format("JSON: {}", error.message), error.offset_start);
}

std::string_view validated(std::string_view text)
{
    if (text.empty()) throw std::invalid_argument("Empty JSON input");
    encoding::validate_utf8(text);
    text = without_bom(text);
    std::size_t offset = 0;
    try
    {
        validate_tokens(text, offset);
    }
    catch (const std::invalid_argument &error)
    {
        throw std::invalid_argument(std::format("{}{}", error.what(), source_location(text, static_cast<std::ptrdiff_t>(offset ? offset - 1 : 0))));
    }
    return text;
}

Json::Value parse_located(std::string_view text)
{
    try
    {
        return parse(text);
    }
    catch (const LocatedError &error)
    {
        throw std::invalid_argument(std::format("{}{}", error.what(), source_location(text, error.offset)));
    }
}
} // namespace

std::string_view without_bom(std::string_view text) { return text.starts_with("\xEF\xBB\xBF") ? text.substr(3) : text; }

Json::Value parse_json(std::string_view text)
{
    return parse_located(validated(text));
}

Json::Value parse_document(std::string_view text, std::size_t depth)
{
    text = validated(text);
    const auto document = skeleton(text, depth);
    try
    {
        auto result = parse(document.text);
        document.relocate(result);
        return result;
    }
    catch (const LocatedError &)
    {
        return parse_located(text);
    }
}

Json::Value parse_record(std::string_view source, const Json::Value &placeholder)
{
    if (!placeholder.isObject()) return placeholder;
    const auto start = placeholder.getOffsetStart();
    return parse(source.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(placeholder.getOffsetLimit() - start)));
}

void object(const Json::Value &value, std::initializer_list<std::string_view> allowed)
{
    if (!value.isObject()) throw std::invalid_argument("Expected JSON object");
    for (const auto &key : value.getMemberNames())
        if (std::ranges::find(allowed, key) == allowed.end()) throw std::invalid_argument(std::format("Unknown JSON property: {}", key));
}

std::int64_t integer(const Json::Value &value)
{
    if ((value.type() != Json::intValue && value.type() != Json::uintValue) || !value.isInt64())
        throw std::invalid_argument("Expected signed 64-bit JSON integer");
    return static_cast<std::int64_t>(value.asInt64());
}

std::size_t count(const Json::Value &value, std::size_t minimum)
{
    const auto n = integer(value);
    if (n < static_cast<std::int64_t>(minimum) || n > static_cast<std::int64_t>(max_values)) throw std::invalid_argument(std::format("Count must be in [{}, 100000]", minimum));
    return static_cast<std::size_t>(n);
}
} // namespace l2asmdisasm::json
