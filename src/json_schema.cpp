#include "json.h"
#include <algorithm>
#include <limits>
#include <set>
#include <unordered_map>

namespace l2asmdisasm::json
{
std::vector<Field> parse_fields(const JsonValue &values, unsigned depth = 0)
{
    if (!values.isArray() || values.empty() || depth > 64) util::fail("Expected nonempty fields array (maximum depth 64)");
    std::vector<Field> result;
    for (const auto &v : values)
    {
        object(v, {"name", "type", "count", "size", "fields", "enabledBy", "encoding", "terminated", "format"});
        Field field{};
        field.name = string(at(v, "name"));
        const auto type = string(at(v, "type"));
        const auto found = std::find_if(std::begin(util::types), std::end(util::types), [&](const auto &info)
                                        { return type == info.name; });
        if (found == std::end(util::types)) util::fail("Unknown binary type: " + type);
        field.type = static_cast<FieldType>(found - std::begin(util::types));
        if (v.isMember("encoding"))
        {
            if (field.type != FieldType::STRING) util::fail("encoding is only valid for string");
            const auto encoding = string(v["encoding"]);
            if (encoding != "byte" && encoding != "utf16") util::fail("string encoding must be byte or utf16");
            field.wide = encoding == "utf16";
        }
        if (v.isMember("terminated")) field.terminated = boolean(v["terminated"]);
        if (v.isMember("format"))
        {
            if (string(v["format"]) != "id-list") util::fail("Unknown string format");
            field.id_list = true;
        }
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
        JsonValue v = make_object({{"name", field.name}, {"type", util::type_info(field.type).name}});
        if (field.wide) v["encoding"] = "utf16";
        if (field.terminated) v["terminated"] = *field.terminated;
        if (field.id_list) v["format"] = "id-list";
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

void validate_fields(const std::vector<Field> &fields, std::unordered_map<std::string, bool> scope = {}, unsigned depth = 0)
{
    if (depth > 64) util::fail("Schema nesting exceeds 64 groups");
    if (fields.empty()) util::fail("Schema has no fields");
    std::set<std::string> local;
    for (const auto &field : fields)
        scope.erase(field.name);
    for (const auto &field : fields)
    {
        (void)util::min_size(field.type);
        if (field.wide && field.type != FieldType::STRING) util::fail("encoding is only valid for string");
        if (field.terminated && field.type != FieldType::STRING && field.type != FieldType::UTF16)
            util::fail("terminated is only valid for string or utf16");
        if (field.id_list && field.type != FieldType::STRING && field.type != FieldType::UTF16)
            util::fail("format is only valid for string or utf16");
        if (field.name.empty() || field.name.find_first_of("\t\r\n") != std::string::npos ||
            field.count == 0 || field.count > limit ||
            (field.type == FieldType::PADDING && field.array) ||
            (field.type == FieldType::GROUP) == field.members.empty() ||
            (!field.array && ((field.count != 1 && field.type != FieldType::PADDING) || !field.counter.empty()))) util::fail("Invalid schema field");
        const auto require = [&](const std::string &name)
        {
            if (!scope.contains(name) || !scope.at(name)) util::fail("Missing preceding integer field: " + name);
        };
        if (!field.counter.empty()) require(field.counter);
        for (const auto &clause : field.enabled_by)
            for (const auto &term : clause)
                require(term.field);
        if (field.type == FieldType::GROUP) validate_fields(field.members, scope, depth + 1);
        if (local.insert(field.name).second) scope[field.name] = util::is_integer(field.type);
    }
}

void validate_schema(const Schema &schema)
{
    if (schema.magic > limit) util::fail("Schema padding exceeds 100000 dwords");
    if (schema.blocks.empty()) util::fail("Schema has no blocks");
    for (std::size_t i = 0; i < schema.blocks.size(); ++i)
    {
        const auto &part = schema.blocks[i];
        if (part.record_count < Block::to_end || part.record_count > std::numeric_limits<std::uint32_t>::max()) util::fail("Invalid record count");
        validate_fields(part.fields);
        if (!part.counted_by.empty())
        {
            const auto field = std::find_if(part.fields.begin(), part.fields.end(), [&](const Field &f)
                                            { return f.name == part.counted_by; });
            if (part.record_count != Block::stored || field == part.fields.end() || !util::is_integer(field->type)) util::fail("Record counter must name an integer field");
        }
        if (i + 1 < schema.blocks.size() && (part.record_count == Block::to_end || !part.counted_by.empty()))
            util::fail("Only the last block may run to the end");
    }
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
    for (const auto &v : definitions)
    {
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
        schema.blocks.push_back(std::move(b));
    }
    validate_schema(schema);
    return schema;
}

JsonValue schema_json(const Schema &schema)
{
    validate_schema(schema);
    JsonValue root = make_object({{"blocks", array()}});
    if (!schema.trailer) root["trailer"] = false;
    if (schema.magic) root["magic"] = Json::UInt64(schema.magic);
    if (!schema.clients.empty())
    {
        root["clients"] = array();
        for (const auto &client : schema.clients)
            root["clients"].append(client);
    }
    for (const auto &block : schema.blocks)
    {
        auto value = make_object({{"fields", write_fields(block.fields)}});
        if (!block.counted_by.empty()) value["recordCount"] = make_object({{"nonzero", block.counted_by}});
        else if (block.record_count == Block::compact) value["recordCount"] = "compact";
        else if (block.record_count == Block::to_end) value["recordCount"] = "eof";
        else if (block.record_count >= 0) value["recordCount"] = Json::Int64(block.record_count);
        root["blocks"].append(std::move(value));
    }
    return root;
}
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
} // namespace l2asmdisasm::json
