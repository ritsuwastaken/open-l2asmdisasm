#include "encoding.h"
#include "json.h"
#include <bit>
#include <charconv>
#include <cmath>

namespace l2asmdisasm::json
{
namespace
{
Json::Value id_list_json(const std::string &text)
{
    auto result = array();
    std::string_view remaining(text);
    while (!remaining.empty())
    {
        const auto comma = remaining.find(',');
        const auto token = remaining.substr(0, comma);
        std::uint32_t id = 0;
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), id);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() ||
            std::to_string(id) != token || result.size() == max_values) return text;
        result.append(Json::UInt(id));
        if (comma == std::string_view::npos) break;
        remaining.remove_prefix(comma + 1);
        if (remaining.empty()) return text;
    }
    return result;
}

std::uint32_t uint32(const Json::Value &value, const std::string &what)
{
    const auto n = located(value, integer);
    if (n < 0 || n > UINT32_MAX) throw LocatedError(what + " must be in [0, 4294967295]", value.getOffsetStart());
    return static_cast<std::uint32_t>(n);
}

std::string id_list_text(const Json::Value &ids)
{
    if (ids.size() > max_values) util::fail("ID list exceeds 100000 values");
    std::string result;
    for (const auto &item : ids)
    {
        if (!result.empty()) result += ',';
        result += std::to_string(uint32(item, "ID"));
    }
    return result;
}

bool default_terminated(const Field &field, const std::string &text)
{
    return field.terminated.value_or(field.type == FieldType::STRING && !text.empty());
}

bool default_wide(const Field &field, const std::string &text, bool terminated)
{
    return field.type == FieldType::UTF16 || (field.wide && (!text.empty() || terminated));
}

Json::Value encode_string(const binary::Value &cell, const Field &field)
{
    auto text = cell.text;
    const bool terminated = text.ends_with('\0');
    if (terminated) text.pop_back();
    const bool same_encoding = cell.wide == default_wide(field, text, terminated);
    const bool same_termination = terminated == default_terminated(field, text);
    auto content = field.format == FieldFormat::IdList ? id_list_json(text) : Json::Value(text);
    if (same_encoding && same_termination) return content;
    auto result = make_object({{"value", std::move(content)}});
    if (!same_encoding) result["encoding"] = cell.wide ? "utf16" : "byte";
    if (!same_termination) result["terminated"] = terminated;
    return result;
}

bool utf16_encoding(const Json::Value &value)
{
    const auto encoding = string(value);
    if (encoding != "byte" && encoding != "utf16") util::fail("string encoding must be byte or utf16");
    return encoding == "utf16";
}

binary::Value decode_string(const Json::Value &value, const Field &field)
{
    const bool overridden = value.isObject();
    const auto &content = overridden ? at(value, "value") : value;
    auto text = located(content, [&]
                        { return field.format == FieldFormat::IdList && content.isArray() ? id_list_text(content) : string(content); });
    auto terminated = default_terminated(field, text);
    if (overridden)
    {
        if (field.type == FieldType::UTF16) object(value, {"value", "terminated"});
        else object(value, {"value", "encoding", "terminated"});
        if (value.isMember("terminated"))
            terminated = located(value["terminated"], boolean);
    }
    auto wide = default_wide(field, text, terminated);
    if (overridden && value.isMember("encoding"))
        wide = located(value["encoding"], utf16_encoding);
    if (terminated) text += '\0';
    return {.text = std::move(text), .wide = wide};
}

double from_bits(FieldType type, std::uint64_t bits)
{
    return type == FieldType::F32 ? double(std::bit_cast<float>(std::uint32_t(bits))) : std::bit_cast<double>(bits);
}

Json::Value encode_float(const binary::Value &cell, FieldType type)
{
    const auto bits = static_cast<std::uint64_t>(cell.integer);
    const auto number = from_bits(type, bits);
    if (!std::isfinite(number)) return make_object({{"bits", util::hex(bits)}});
    return number;
}

binary::Value decode_float_bits(const Json::Value &value, FieldType type)
{
    const auto bits = util::number<std::uint64_t>(string(value), 16);
    if (type == FieldType::F32 && bits > UINT32_MAX) util::fail("f32 bits overflow");
    if (std::isfinite(from_bits(type, bits))) util::fail("bits is reserved for non-finite floats");
    return {std::bit_cast<std::int64_t>(bits)};
}

binary::Value decode_float(const Json::Value &value, FieldType type)
{
    if (value.isObject())
    {
        object(value, {"bits"});
        const auto &node = at(value, "bits");
        return located(node, [&]
                       { return decode_float_bits(node, type); });
    }
    if (!value.isNumeric()) util::fail("Expected JSON number");
    const auto number = value.asDouble();
    if (!std::isfinite(number) || (type == FieldType::F32 && !std::isfinite(static_cast<float>(number)))) util::fail("Float overflow");
    if (type == FieldType::F32) return {std::bit_cast<std::uint32_t>(static_cast<float>(number))};
    return {std::bit_cast<std::int64_t>(number)};
}

binary::Value decode_integer(const Json::Value &value, FieldType type)
{
    const auto n = integer(value);
    const bool is_signed = util::type_info(type).is_signed;
    const auto width = util::type_info(type).width;
    const auto minimum = is_signed ? -(INT64_C(1) << (width - 1)) : 0;
    const auto maximum = (INT64_C(1) << (width - (is_signed ? 1 : 0))) - 1;
    if (n < minimum || n > maximum) util::fail("Integer field overflow");
    return {n};
}

binary::Value decode_name_ref(const Json::Value &value, const Field &field, NameLookup &lookup)
{
    if (!lookup.names) util::fail("Name table required for resolved references");
    const auto stored = [&](std::uint32_t index)
    { return field.type == FieldType::I32 ? binary::Value{std::bit_cast<std::int32_t>(index)} : binary::Value{index}; };
    if (value.isString()) return stored(lookup.find_or_append(value.asString()));
    object(value, {"index", "value"});
    const auto index = uint32(at(value, "index"), "Name index");
    const auto &text_node = at(value, "value");
    const auto text = located(text_node, string);
    if (index < lookup.names->size() && (*lookup.names)[index] == text) return stored(index);
    return stored(located(text_node, [&]
                          { return lookup.find_or_append(text); }));
}

std::string fold_case(std::string text)
{
    for (auto &c : text)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return text;
}
} // namespace

std::uint32_t NameLookup::find_or_append(const std::string &text)
{
    for (; indexed < names->size(); ++indexed)
        indexes.try_emplace(fold_case((*names)[indexed]), static_cast<std::uint32_t>(indexed));
    auto key = fold_case(text);
    if (const auto found = indexes.find(key); found != indexes.end()) return found->second;
    if (names->size() >= UINT32_MAX) util::fail("Name table is full");
    const auto index = static_cast<std::uint32_t>(names->size());
    names->push_back(text);
    indexes.emplace(std::move(key), index);
    indexed = names->size();
    return index;
}

Json::Value encode_field(const binary::Value &cell, const Field &field, const NameTable *names)
{
    if (field.format == FieldFormat::NameRef && names && static_cast<std::uint32_t>(cell.integer) < names->size())
    {
        const auto index = static_cast<std::uint32_t>(cell.integer);
        encoding::validate_utf8((*names)[index]);
        return make_object({{"index", Json::UInt(index)}, {"value", (*names)[index]}});
    }
    switch (field.type)
    {
    case FieldType::STRING:
    case FieldType::UTF16:
        return encode_string(cell, field);
    case FieldType::F32:
    case FieldType::F64:
        return encode_float(cell, field.type);
    case FieldType::I64:
        return std::to_string(cell.integer);
    default:
        return Json::Int64(cell.integer);
    }
}

binary::Value decode_field(const Json::Value &value, const Field &field, NameLookup &lookup)
{
    if (field.format == FieldFormat::NameRef && (value.isObject() || value.isString())) return decode_name_ref(value, field, lookup);
    switch (field.type)
    {
    case FieldType::STRING:
    case FieldType::UTF16:
        return decode_string(value, field);
    case FieldType::F32:
    case FieldType::F64:
        return decode_float(value, field.type);
    case FieldType::I64:
        if (!value.isString()) util::fail("i64 requires a decimal string");
        return {util::number<std::int64_t>(value.asString())};
    default:
        return decode_integer(value, field.type);
    }
}
} // namespace l2asmdisasm::json
