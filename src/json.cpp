#include "binary.h"
#include "layout.h"
#include "util.h"
#include "value.h"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <json/json.h>
#include <memory>
#include <set>
#include <unordered_map>

namespace l2asmdisasm
{
namespace
{
using JsonValue = Json::Value;
constexpr std::string_view marker("\x0cSafePackage\0", 13);
constexpr std::string_view types[] = {"u32", "hex32", "i32", "u16", "i16", "u8", "hex8", "i8", "f32",
                                      "unicode", "ascf", "compact", "filler", "i64", "f64", "group", "mtx", "mtx2", "mtx3", "mat", "mat2"};

JsonValue make_object(std::initializer_list<std::pair<std::string, JsonValue>> members)
{
    JsonValue result(Json::objectValue);
    for (const auto &[name, value] : members)
        result[name] = value;
    return result;
}

JsonValue array(std::initializer_list<JsonValue> values = {})
{
    JsonValue result(Json::arrayValue);
    for (const auto &value : values)
        result.append(value);
    return result;
}

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
    const auto text = value.asString();
    encoding::validate_utf8(text);
    return text;
}

bool boolean(const JsonValue &value)
{
    if (!value.isBool()) util::fail("Expected JSON boolean");
    return value.asBool();
}

using RealNumbers = std::unordered_map<std::ptrdiff_t, double>;
RealNumbers normalize_tokens(std::string &text)
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
    std::size_t i = 0;
    while (i < text.size())
    {
        const auto c = text[i++];
        if (c == '"')
        {
            bool closed = false;
            while (i < text.size())
            {
                const auto ch = text[i++];
                if (ch == '"')
                {
                    closed = true;
                    break;
                }
                if (static_cast<unsigned char>(ch) < 0x20) util::fail("Unescaped JSON control character");
                if (ch != '\\') continue;
                if (i == text.size()) util::fail("Truncated JSON escape");
                const auto escape = text[i++];
                if (escape != 'u')
                {
                    if (std::string_view("\"\\/bfnrt").find(escape) == std::string_view::npos) util::fail("Invalid JSON escape");
                    continue;
                }
                const auto high = hex_unit(i);
                if (high >= 0xdc00 && high <= 0xdfff) util::fail("Unpaired JSON Unicode surrogate");
                if (high < 0xd800 || high > 0xdbff) continue;
                if (text.substr(i, 2) != "\\u") util::fail("Unpaired JSON Unicode surrogate");
                i += 2;
                const auto low = hex_unit(i);
                if (low < 0xdc00 || low > 0xdfff) util::fail("Invalid JSON Unicode surrogate pair");
            }
            if (!closed) util::fail("Unterminated JSON string");
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
    if (value.isArray() || value.isObject())
    {
        for (auto &child : value)
            restore_numbers(child, numbers);
    }
    else if (const auto found = numbers.find(value.getOffsetStart()); found != numbers.end()) value = found->second;
}

JsonValue parse_json(std::string_view text)
{
    if (text.empty()) util::fail("Empty JSON input");
    encoding::validate_utf8(text);
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    std::string document(text);
    const auto numbers = normalize_tokens(document);
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

std::string write_json(const JsonValue &value)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    builder["commentStyle"] = "None";
    builder["enableYAMLCompatibility"] = true;
    builder["emitUTF8"] = true;
    builder["precision"] = 17;
    return Json::writeString(builder, value) + "\n";
}

template <typename Function>
auto checked_json(Function function)
{
    try
    {
        return function();
    }
    catch (const Json::Exception &error)
    {
        util::fail(std::string("JSON: ") + error.what());
    }
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

std::size_t count(const JsonValue &value, std::size_t minimum = 0)
{
    const auto n = integer(value);
    if (n < static_cast<std::int64_t>(minimum) || n > static_cast<std::int64_t>(layout::limit)) util::fail("Count must be in [" + std::to_string(minimum) + ", 100000]");
    return static_cast<std::size_t>(n);
}

std::vector<Field> parse_fields(const JsonValue &values, unsigned depth = 0)
{
    if (!values.isArray() || values.empty() || depth > 64) util::fail("Expected nonempty fields array (maximum depth 64)");
    std::vector<Field> result;
    for (const auto &v : values)
    {
        object(v, {"name", "type", "count", "size", "fields", "enabledBy"});
        Field field{};
        field.name = string(at(v, "name"));
        const auto type = string(at(v, "type"));
        const auto found = std::find(std::begin(types), std::end(types), type);
        if (found == std::end(types)) util::fail("Unknown binary type: " + type);
        field.type = static_cast<FieldType>(found - std::begin(types));
        if (v.isMember("count"))
        {
            field.array = true;
            if (v["count"].isString()) field.counter = string(v["count"]);
            else field.count = count(v["count"], 1);
            if (v["count"].isString() && field.counter.empty()) util::fail("Empty array counter");
        }
        if (field.type == FieldType::FILLER) field.count = count(at(v, "size"), 1);
        else if (v.isMember("size")) util::fail("size is only valid for filler");
        if (field.type == FieldType::GROUP) field.members = parse_fields(at(v, "fields"), depth + 1);
        else if (v.isMember("fields")) util::fail("fields is only valid for group");
        if (v.isMember("enabledBy"))
        {
            if (!v["enabledBy"].isArray() || v["enabledBy"].empty()) util::fail("enabledBy must be a nonempty array of clauses");
            for (const auto &clause : v["enabledBy"])
            {
                if (!clause.isArray() || clause.empty()) util::fail("Expected nonempty condition clause");
                auto &terms = field.enabled_by.emplace_back();
                for (const auto &term : clause)
                {
                    object(term, {"field", "value", "mask"});
                    terms.push_back({string(at(term, "field")), integer(at(term, "value")), term.isMember("mask") ? integer(term["mask"]) : -1});
                }
            }
        }
        result.push_back(std::move(field));
    }
    return result;
}

JsonValue write_fields(const std::vector<Field> &fields)
{
    auto result = array();
    for (const auto &field : fields)
    {
        JsonValue v = make_object({{"name", field.name}, {"type", std::string(types[static_cast<std::size_t>(field.type)])}});
        if (field.array) v["count"] = field.counter.empty() ? JsonValue(Json::UInt64(field.count)) : JsonValue(field.counter);
        if (field.type == FieldType::FILLER) v["size"] = Json::UInt64(field.count);
        if (field.type == FieldType::GROUP) v["fields"] = write_fields(field.members);
        if (!field.enabled_by.empty())
        {
            v["enabledBy"] = array();
            for (const auto &clause : field.enabled_by)
            {
                auto terms = array();
                for (const auto &term : clause)
                {
                    JsonValue condition = make_object({{"field", term.field}, {"value", Json::Int64(term.value)}});
                    if (term.mask != -1) condition["mask"] = Json::Int64(term.mask);
                    terms.append(condition);
                }
                v["enabledBy"].append(terms);
            }
        }
        result.append(v);
    }
    return result;
}

void references(const std::vector<Field> &fields, std::unordered_map<std::string, bool> scope = {}, unsigned depth = 0)
{
    if (depth > 64) util::fail("Schema nesting exceeds 64 groups");
    std::set<std::string> local;
    for (const auto &field : fields)
        scope.erase(field.name);
    for (const auto &field : fields)
    {
        const auto require = [&](const std::string &name)
        {
            if (!scope.contains(name) || !scope.at(name)) util::fail("Missing preceding integer field: " + name);
        };
        if (!field.counter.empty()) require(field.counter);
        for (const auto &clause : field.enabled_by)
            for (const auto &term : clause)
                require(term.field);
        if (field.type == FieldType::GROUP) references(field.members, scope, depth + 1);
        if (local.insert(field.name).second) scope[field.name] = util::is_integer(field.type);
    }
}

std::vector<Block> blocks(const Schema &schema)
{
    std::vector<Block> result{static_cast<const Block &>(schema)};
    result.insert(result.end(), schema.blocks.begin(), schema.blocks.end());
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        Schema part;
        static_cast<Block &>(part) = result[i];
        part.magic = schema.magic;
        layout::validate(part, layout::Index(part));
        references(part.fields);
        if (i + 1 < result.size() && (part.record_count == Block::to_end || !part.counted_by.empty()))
            util::fail("Only the last block may run to the end");
    }
    return result;
}

Field scalar(FieldType type, std::string name, std::string counter = {})
{
    Field f{};
    f.type = type;
    f.name = std::move(name);
    f.counter = std::move(counter);
    f.array = !f.counter.empty();
    return f;
}

std::vector<Field> compound(FieldType type)
{
    const bool material = type >= FieldType::MAT;
    std::vector<Field> fields{scalar(FieldType::INT, material ? "count" : "meshCount")};
    if (type == FieldType::MAT2) fields.push_back(scalar(FieldType::INT, "extra"));
    auto entry = scalar(FieldType::UNICODE, material ? "entries" : "meshes", material ? "count" : "meshCount");
    if (type != FieldType::MTX)
    {
        entry.type = FieldType::GROUP;
        if (material) entry.members = {scalar(FieldType::INT, "id"), scalar(FieldType::INT, "count")};
        else
        {
            entry.members = {scalar(FieldType::UNICODE, "name"), scalar(FieldType::UCHAR, "bytes")};
            if (type == FieldType::MTX2)
            {
                entry.members.back().name = "byte";
                entry.members.push_back(scalar(FieldType::UINT, "value"));
            }
            else
            {
                entry.members.back().array = true;
                entry.members.back().count = 2;
            }
        }
    }
    fields.push_back(entry);
    if (!material)
    {
        fields.push_back(scalar(FieldType::INT, "textureCount"));
        fields.push_back(scalar(FieldType::UNICODE, "textures", "textureCount"));
    }
    if (type == FieldType::MTX3) fields.push_back(scalar(FieldType::UNICODE, "extraTexture"));
    return fields;
}

std::vector<std::string> field_keys(const std::vector<Field> &fields)
{
    std::set<std::string> reserved, used;
    for (const auto &field : fields)
        reserved.insert(field.name);
    std::vector<std::string> keys;
    for (const auto &field : fields)
    {
        auto key = field.name;
        for (std::size_t suffix = 2; used.contains(key); ++suffix)
        {
            key = field.name + "#" + std::to_string(suffix);
            while (reserved.contains(key))
                key += '#';
        }
        used.insert(key);
        keys.push_back(key);
    }
    return keys;
}

JsonValue encode_value(const value::Value &v, FieldType type)
{
    if (type == FieldType::UNICODE) return v.text;
    if (type == FieldType::ASCF) return make_object({{"encoding", v.wide ? "utf16" : "byte"}, {"value", v.text}});
    if (type == FieldType::INT64) return std::to_string(v.integer);
    if (type == FieldType::FLOAT || type == FieldType::DOUBLE)
    {
        const double number = type == FieldType::FLOAT ? double(std::bit_cast<float>(std::uint32_t(v.integer))) : std::bit_cast<double>(v.integer);
        if (!std::isfinite(number)) return make_object({{"bits", util::hex(static_cast<std::uint64_t>(v.integer) & (type == FieldType::FLOAT ? UINT64_C(0xffffffff) : UINT64_MAX))}});
        return number;
    }
    return JsonValue(Json::Int64(v.integer));
}

value::Value decode_value(const JsonValue &v, FieldType type)
{
    if (type == FieldType::UNICODE) return {string(v), true};
    if (type == FieldType::ASCF)
    {
        object(v, {"encoding", "value"});
        const auto encoding = string(at(v, "encoding"));
        if (encoding != "byte" && encoding != "utf16") util::fail("ASCF encoding must be byte or utf16");
        return {string(at(v, "value")), encoding == "utf16"};
    }
    if (type == FieldType::FLOAT || type == FieldType::DOUBLE)
    {
        if (v.isObject())
        {
            object(v, {"bits"});
            const auto bits = util::number<std::uint64_t>(string(at(v, "bits")), 16);
            if (type == FieldType::FLOAT && bits > UINT32_MAX) util::fail("f32 bits overflow");
            const double number = type == FieldType::FLOAT ? double(std::bit_cast<float>(std::uint32_t(bits))) : std::bit_cast<double>(bits);
            if (std::isfinite(number)) util::fail("bits is reserved for non-finite floats");
            return {std::bit_cast<std::int64_t>(bits)};
        }
        if (!v.isNumeric()) util::fail("Expected JSON number");
        const auto number = v.asDouble();
        if (!std::isfinite(number) || (type == FieldType::FLOAT && !std::isfinite(static_cast<float>(number)))) util::fail("Float overflow");
        if (type == FieldType::FLOAT) return {std::bit_cast<std::uint32_t>(static_cast<float>(number))};
        return {std::bit_cast<std::int64_t>(number)};
    }
    if (type == FieldType::INT64)
    {
        if (!v.isString()) util::fail("i64 requires a decimal string");
        return value::parse(string(v), type, {});
    }
    const auto n = integer(v);
    if (util::is_hexadecimal(type))
    {
        if (n < 0 || n > (type == FieldType::CHEX ? 255 : INT64_C(4294967295))) util::fail("Unsigned field overflow");
        return {n};
    }
    return value::parse(std::to_string(n), type, {});
}

struct Scope
{
    std::unordered_map<std::string, std::int64_t> values;
    const Scope *parent;
    std::int64_t get(const std::string &name) const
    {
        if (const auto it = values.find(name); it != values.end()) return it->second;
        if (parent) return parent->get(name);
        util::fail("Missing preceding integer field: " + name);
    }
};

struct Records
{
    binary::Reader *reader;
    std::vector<std::uint8_t> *output;
    const encoding::Conversion &conversion;
    std::size_t cells = 0;

    JsonValue record(const std::vector<Field> &fields, const JsonValue *input, const Scope *parent = nullptr, const std::string &path = {})
    {
        if (input && !input->isObject()) util::fail(path + ": Expected record object");
        const auto keys = field_keys(fields);
        Scope scope{{}, parent};
        auto result = JsonValue(Json::objectValue);
        std::set<std::string> seen;
        for (std::size_t f = 0; f < fields.size(); ++f)
        {
            const auto &field = fields[f];
            const auto &key = keys[f];
            const auto where = path + key;
            try
            {
                bool enabled = field.enabled_by.empty();
                for (const auto &clause : field.enabled_by)
                    enabled |= std::all_of(clause.begin(), clause.end(), [&](const Condition &c)
                                           {
                                               return (std::uint32_t(scope.get(c.field)) & std::uint32_t(c.mask)) == std::uint32_t(c.value);
                                           });
                if (!enabled)
                {
                    if (input && input->isMember(key)) util::fail("Disabled field must be omitted");
                    if (seen.insert(field.name).second) scope.values[field.name] = 0;
                    continue;
                }
                const auto n = field.counter.empty() ? (field.type == FieldType::FILLER ? 1 : field.count) : count(JsonValue(Json::Int64(scope.get(field.counter))));
                if (n > layout::limit || cells > layout::limit - std::max(n, std::size_t{1})) util::fail("Record exceeds 100000 values");
                cells += std::max(n, std::size_t{1});
                const JsonValue *source = input ? &at(*input, key) : nullptr;
                if (field.array && source && (!source->isArray() || source->size() != n)) util::fail("Array length differs from count");
                auto values = array();
                std::int64_t first = 0;
                for (std::size_t i = 0; i < n; ++i)
                {
                    const auto *element = source ? (field.array ? &at(*source, i) : source) : nullptr;
                    JsonValue v;
                    if (field.type == FieldType::GROUP || field.type >= FieldType::MTX)
                    {
                        const auto members = field.type == FieldType::GROUP ? field.members : compound(field.type);
                        v = record(members, element, &scope, where + (field.array ? "[" + std::to_string(i) + "]" : "") + ".");
                    }
                    else
                    {
                        const auto cell = reader ? binary::read(*reader, field.type, field.type == FieldType::FILLER ? field.count : 0, conversion) : decode_value(*element, field.type);
                        if (!reader) binary::write(*output, field.type, cell, field.type == FieldType::FILLER ? field.count : 0, conversion);
                        if (i == 0) first = cell.integer;
                        v = encode_value(cell, field.type);
                    }
                    values.append(std::move(v));
                }
                if (seen.insert(field.name).second) scope.values[field.name] = first;
                result[key] = field.array ? std::move(values) : std::move(at(values, 0));
            }
            catch (const std::invalid_argument &error)
            {
                util::fail(where + ": " + error.what());
            }
            catch (const Json::Exception &error)
            {
                util::fail(where + ": " + error.what());
            }
        }
        if (input)
            for (const auto &key : input->getMemberNames())
                if (!result.isMember(key)) util::fail(path + key + ": Unknown record field");
        return result;
    }
};

bool nonzero(const JsonValue &record, const std::string &name)
{
    if (!record.isMember(name)) return false;
    const auto &v = at(record, name);
    return v.isArray() ? (!v.empty() && integer(at(v, 0)) != 0) : integer(v) != 0;
}

bool portable_component(std::string_view name)
{
    return !name.empty() && name != "." && name != ".." &&
           std::all_of(name.begin(), name.end(), [](unsigned char c)
                       {
                           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
                       });
}

std::string dat_name(std::string_view filename)
{
    if (!portable_component(filename)) util::fail("Expected a DAT basename without directories");
    std::string name(filename);
    for (auto &c : name)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (name.ends_with(".dat")) name.resize(name.size() - 4);
    else if (name.ends_with(".json")) name.resize(name.size() - 5);
    if (!portable_component(name)) util::fail("Empty DAT basename");
    return name;
}

TextOptions json_options(const TextOptions &options)
{
    TextOptions result;
    result.charset = options.charset;
    return result;
}
} // namespace

namespace
{
Schema schema_value(const JsonValue &root)
{
    return checked_json([&]
                        {
                            object(root, {"$schema", "version", "blocks", "magic", "trailer", "clients"});
                            if (root.isMember("$schema")) (void)string(root["$schema"]);
                            if (root.isMember("version") && integer(root["version"]) != 1) util::fail("Unsupported schema version");
                            Schema schema;
                            if (root.isMember("clients"))
                            {
                                const auto &clients = root["clients"];
                                if (!clients.isArray() || clients.empty()) util::fail("Expected nonempty clients array");
                                std::set<std::string> unique;
                                for (const auto &client : clients)
                                {
                                    const auto name = string(client);
                                    if (!portable_component(name) || !unique.insert(name).second) util::fail("Invalid or duplicate client version: " + name);
                                    schema.clients.push_back(name);
                                }
                            }
                            if (root.isMember("magic")) schema.magic = count(root["magic"]);
                            if (root.isMember("trailer")) schema.trailer = boolean(root["trailer"]);
                            const auto &definitions = at(root, "blocks");
                            if (!definitions.isArray() || definitions.empty()) util::fail("Expected nonempty blocks array");
                            for (std::size_t i = 0; i < definitions.size(); ++i)
                            {
                                const auto &v = at(definitions, i);
                                object(v, {"fields", "recordCount"});
                                Block b;
                                b.fields = parse_fields(at(v, "fields"));
                                if (v.isMember("recordCount"))
                                {
                                    const auto &n = v["recordCount"];
                                    if (n.isString())
                                    {
                                        const auto mode = string(n);
                                        if (mode == "u32") b.record_count = Block::stored;
                                        else if (mode == "compact") b.record_count = Block::compact;
                                        else if (mode == "eof") b.record_count = Block::to_end;
                                        else util::fail("Unknown recordCount mode: " + mode);
                                    }
                                    else if (n.isObject())
                                    {
                                        object(n, {"nonzero"});
                                        b.counted_by = string(at(n, "nonzero"));
                                        if (b.counted_by.empty()) util::fail("Empty nonzero field");
                                    }
                                    else
                                    {
                                        b.record_count = integer(n);
                                        if (b.record_count < 0) util::fail("Fixed recordCount must be nonnegative");
                                    }
                                }
                                if (i == 0) static_cast<Block &>(schema) = std::move(b);
                                else schema.blocks.push_back(std::move(b));
                            }
                            (void)blocks(schema);
                            return schema;
                        });
}

JsonValue schema_json(const Schema &schema)
{
    return checked_json([&]
                        {
                            JsonValue root = make_object({{"version", 1}, {"trailer", schema.trailer}, {"magic", Json::UInt64(schema.magic)}, {"blocks", array()}});
                            if (!schema.clients.empty())
                            {
                                root["clients"] = array();
                                for (const auto &client : schema.clients)
                                    root["clients"].append(client);
                            }
                            for (const auto &block : blocks(schema))
                            {
                                JsonValue n;
                                if (!block.counted_by.empty()) n = make_object({{"nonzero", block.counted_by}});
                                else if (block.record_count == Block::stored) n = "u32";
                                else if (block.record_count == Block::compact) n = "compact";
                                else if (block.record_count == Block::to_end) n = "eof";
                                else n = Json::Int64(block.record_count);
                                root["blocks"].append(make_object({{"recordCount", n}, {"fields", write_fields(block.fields)}}));
                            }
                            return root;
                        });
}
} // namespace

std::vector<Schema> parse_schemas(std::string_view text)
{
    return checked_json([&]
                        {
                            const auto root = parse_json(text);
                            if (!root.isArray()) return std::vector<Schema>{schema_value(root)};
                            if (root.empty()) util::fail("Expected nonempty schema array");
                            std::vector<Schema> schemas;
                            std::set<std::string> clients;
                            for (const auto &value : root)
                            {
                                auto schema = schema_value(value);
                                if (schema.clients.empty()) util::fail("Versioned layouts require clients");
                                for (const auto &client : schema.clients)
                                    if (!clients.insert(client).second) util::fail("Ambiguous definition for client version " + client);
                                schemas.push_back(std::move(schema));
                            }
                            return schemas;
                        });
}

Schema parse_schema(std::string_view text, std::string_view client_version)
{
    auto schemas = parse_schemas(text);
    if (client_version.empty())
    {
        if (schemas.size() != 1) util::fail("Client version required for multiple layouts");
        return std::move(schemas.front());
    }
    for (auto &schema : schemas)
        if (std::find(schema.clients.begin(), schema.clients.end(), client_version) != schema.clients.end()) return std::move(schema);
    util::fail("No definition for client version " + std::string(client_version));
}

std::string export_schema(const Schema &schema)
{
    return checked_json([&]
                        {
                            return write_json(schema_json(schema));
                        });
}

std::string export_schemas(std::span<const Schema> schemas)
{
    return checked_json([&]
                        {
                            if (schemas.empty()) util::fail("Expected nonempty schema array");
                            auto root = array();
                            std::set<std::string> clients;
                            for (const auto &schema : schemas)
                            {
                                if (schema.clients.empty()) util::fail("Versioned layouts require clients");
                                for (const auto &client : schema.clients)
                                    if (!clients.insert(client).second) util::fail("Ambiguous definition for client version " + client);
                                root.append(schema_json(schema));
                            }
                            return write_json(root);
                        });
}

std::filesystem::path resolve_definition(const std::filesystem::path &directory, std::string_view filename)
{
    const auto name = dat_name(filename);
    const auto file = directory / (name + ".json");
    if (!std::filesystem::is_regular_file(file)) util::fail("Unknown DAT filename: " + name);
    return file;
}

std::string disassemble(std::span<const std::uint8_t> input, const Schema &schema, const TextOptions &options)
{
    return checked_json([&]
                        {
                            const auto parts = blocks(schema);
                            const auto text = json_options(options);
                            const encoding::Conversion conversion(text);
                            binary::Reader reader{input};
                            Records walker{&reader, nullptr, conversion};
                            const auto tail = schema.magic * 4 + (schema.trailer ? marker.size() : 0);
                            if (input.size() < tail) util::fail("Truncated DAT trailer");
                            const auto data_end = input.size() - tail;
                            auto output = array();
                            for (std::size_t b = 0; b < parts.size(); ++b)
                            {
                                const auto &block = parts[b];
                                const bool open = block.record_count == Block::to_end || !block.counted_by.empty();
                                std::uint32_t stored = 0;
                                if (block.record_count == Block::stored) stored = reader.integer(4);
                                else if (block.record_count == Block::compact)
                                {
                                    const auto n = reader.compact();
                                    if (n < 0) util::fail("Negative compact record count");
                                    stored = static_cast<std::uint32_t>(n);
                                }
                                else if (!open) stored = static_cast<std::uint32_t>(block.record_count);
                                auto records = array();
                                std::uint64_t flagged = 0;
                                for (std::uint64_t row = 0; open ? reader.offset < data_end : row < stored; ++row)
                                {
                                    const auto offset = reader.offset;
                                    walker.cells = 0;
                                    try
                                    {
                                        auto record = walker.record(block.fields, nullptr);
                                        if (reader.offset == offset) util::fail("Record consumes no bytes");
                                        if (reader.offset > data_end) util::fail("Record overlaps DAT trailer");
                                        if (!block.counted_by.empty() && nonzero(record, block.counted_by)) ++flagged;
                                        records.append(std::move(record));
                                    }
                                    catch (const std::invalid_argument &error)
                                    {
                                        util::fail("Block " + std::to_string(b + 1) + ", record " + std::to_string(row + 1) + ", byte " + std::to_string(offset) + ": " + error.what());
                                    }
                                }
                                if (!block.counted_by.empty() && flagged != stored) util::fail("Stored record count differs from nonzero count");
                                output.append(std::move(records));
                            }
                            for (std::size_t i = 0; i < schema.magic; ++i)
                                if (reader.integer(4) != 0) util::fail("Nonzero MAGIC dword");
                            if (schema.trailer)
                            {
                                const auto bytes = reader.take(marker.size());
                                if (!std::equal(bytes.begin(), bytes.end(), marker.begin())) util::fail("Invalid SafePackage trailer");
                            }
                            if (reader.offset != input.size()) util::fail("Unexpected trailing DAT bytes");
                            return write_json(parts.size() == 1 ? at(output, 0) : make_object({{"blocks", output}}));
                        });
}

std::vector<std::uint8_t> assemble(std::string_view json, const Schema &schema, const TextOptions &options)
{
    return checked_json([&]
                        {
                            const auto parts = blocks(schema);
                            const auto root = parse_json(json);
                            JsonValue input;
                            if (parts.size() == 1) input = array({root});
                            else
                            {
                                object(root, {"blocks"});
                                input = at(root, "blocks");
                            }
                            if (!input.isArray() || input.size() != parts.size()) util::fail("JSON block count differs from schema");
                            const auto text = json_options(options);
                            const encoding::Conversion conversion(text);
                            std::vector<std::uint8_t> output;
                            for (std::size_t b = 0; b < parts.size(); ++b)
                            {
                                const auto &block = parts[b];
                                const auto &records = at(input, b);
                                if (!records.isArray()) util::fail("Expected array of records");
                                if (records.size() > UINT32_MAX) util::fail("Too many records");
                                if (block.record_count >= 0 && records.size() != std::uint64_t(block.record_count)) util::fail("JSON record count differs from fixed recordCount");
                                std::vector<std::uint8_t> body;
                                Records walker{nullptr, &body, conversion};
                                std::uint64_t flagged = 0;
                                for (std::size_t row = 0; row < records.size(); ++row)
                                {
                                    walker.cells = 0;
                                    try
                                    {
                                        const auto offset = body.size();
                                        const auto record = walker.record(block.fields, &at(records, row));
                                        if (body.size() == offset) util::fail("Record consumes no bytes");
                                        if (!block.counted_by.empty() && nonzero(record, block.counted_by)) ++flagged;
                                    }
                                    catch (const std::invalid_argument &error)
                                    {
                                        util::fail("Block " + std::to_string(b + 1) + ", record " + std::to_string(row + 1) + ": " + error.what());
                                    }
                                }
                                const auto n = block.counted_by.empty() ? records.size() : flagged;
                                if (block.record_count == Block::stored) binary::put(output, static_cast<std::uint32_t>(n), 4);
                                else if (block.record_count == Block::compact)
                                {
                                    if (n > INT32_MAX) util::fail("Too many compact-counted records");
                                    const auto bytes = binary::encode_compact_index(static_cast<std::int32_t>(n));
                                    output.insert(output.end(), bytes.begin(), bytes.end());
                                }
                                output.insert(output.end(), body.begin(), body.end());
                            }
                            for (std::size_t i = 0; i < schema.magic; ++i)
                                binary::put(output, 0, 4);
                            if (schema.trailer) output.insert(output.end(), marker.begin(), marker.end());
                            return output;
                        });
}
} // namespace l2asmdisasm
