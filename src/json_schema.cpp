#include "json.h"
#include "layout.h"
#include <algorithm>
#include <set>
#include <unordered_map>

namespace l2asmdisasm::json
{
constexpr std::string_view types[] = {"u32", "hex32", "i32", "u16", "i16", "u8", "hex8", "i8", "f32",
                                      "utf16", "string", "compact", "padding", "i64", "f64", "group"};

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
        if (field.type == FieldType::PADDING) field.count = count(at(v, "size"), 1);
        else if (v.isMember("size")) util::fail("size is only valid for padding");
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
        if (field.type == FieldType::PADDING) v["size"] = Json::UInt64(field.count);
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

bool portable_component(std::string_view name)
{
    return !name.empty() && name != "." && name != ".." &&
           std::all_of(name.begin(), name.end(), [](unsigned char c)
                       { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'; });
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

Schema schema_value(const JsonValue &root)
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
}

JsonValue schema_json(const Schema &schema)
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
}
} // namespace l2asmdisasm::json

namespace l2asmdisasm
{
using namespace json;
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
                            return schemas; });
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
                        { return write_json(schema_json(schema)); });
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
                            return write_json(root); });
}

std::filesystem::path resolve_definition(const std::filesystem::path &directory, std::string_view filename)
{
    const auto name = dat_name(filename);
    const auto file = directory / (name + ".json");
    if (!std::filesystem::is_regular_file(file)) util::fail("Unknown DAT filename: " + name);
    return file;
}
} // namespace l2asmdisasm
