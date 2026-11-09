#include "json.h"
#include <algorithm>
#include <format>
#include <limits>
#include <set>
#include <unordered_map>

namespace l2asmdisasm::json
{
std::vector<Field> parse_fields(const Json::Value &values, unsigned depth = 0)
{
    if (!values.isArray() || values.empty() || depth > 64) throw std::invalid_argument("Expected nonempty fields array (maximum depth 64)");
    std::vector<Field> result;
    for (const auto &v : values)
    {
        object(v, {"name", "type", "count", "size", "fields", "enabledBy", "encoding", "terminated", "format"});
        Field field{};
        field.name = string(at(v, "name"));
        const auto type = string(at(v, "type"));
        const auto found = std::ranges::find(binary::types, type, &binary::TypeInfo::name);
        if (found == std::end(binary::types)) throw std::invalid_argument(std::format("Unknown binary type: {}", type));
        field.type = static_cast<FieldType>(found - std::begin(binary::types));
        if (v.isMember("encoding"))
        {
            if (field.type != FieldType::STRING) throw std::invalid_argument("encoding is only valid for string");
            const auto encoding = string(v["encoding"]);
            if (encoding != "byte" && encoding != "utf16") throw std::invalid_argument("string encoding must be byte or utf16");
            field.wide = encoding == "utf16";
        }
        if (v.isMember("terminated")) field.terminated = boolean(v["terminated"]);
        if (v.isMember("format"))
        {
            const auto format = string(v["format"]);
            if (format == "id-list") field.format = FieldFormat::IdList;
            else if (format == "name-ref") field.format = FieldFormat::NameRef;
            else throw std::invalid_argument("Unknown field format");
        }
        if (v.isMember("count"))
        {
            field.array = true;
            if (v["count"].isString()) field.counter = string(v["count"]);
            else field.count = count(v["count"], 1);
            if (v["count"].isString() && field.counter.empty()) throw std::invalid_argument("Empty array counter");
        }
        if (field.type == FieldType::PADDING) field.size = count(at(v, "size"), 1);
        else if (v.isMember("size")) throw std::invalid_argument("size is only valid for padding");
        if (field.type == FieldType::GROUP) field.members = parse_fields(at(v, "fields"), depth + 1);
        else if (v.isMember("fields")) throw std::invalid_argument("fields is only valid for group");
        if (v.isMember("enabledBy"))
        {
            if (!v["enabledBy"].isArray() || v["enabledBy"].empty()) throw std::invalid_argument("enabledBy must be a nonempty array of clauses");
            for (const auto &clause : v["enabledBy"])
            {
                if (!clause.isArray() || clause.empty()) throw std::invalid_argument("Expected nonempty condition clause");
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

enum class Scoped
{
    Other,
    Integer,
    NameRef
};

void validate_fields(const std::vector<Field> &fields, std::unordered_map<std::string, Scoped> scope = {}, unsigned depth = 0)
{
    if (depth > 64) throw std::invalid_argument("Schema nesting exceeds 64 groups");
    if (fields.empty()) throw std::invalid_argument("Schema has no fields");
    std::set<std::string> local;
    for (const auto &field : fields)
        scope.erase(field.name);
    for (const auto &field : fields)
    {
        (void)binary::type_info(field.type);
        if (field.wide && field.type != FieldType::STRING) throw std::invalid_argument("encoding is only valid for string");
        if (field.terminated && field.type != FieldType::STRING && field.type != FieldType::UTF16)
            throw std::invalid_argument("terminated is only valid for string or utf16");
        if (field.format != FieldFormat::Plain && field.format != FieldFormat::IdList && field.format != FieldFormat::NameRef)
            throw std::invalid_argument("Unknown field format");
        if (field.format == FieldFormat::IdList && field.type != FieldType::STRING && field.type != FieldType::UTF16)
            throw std::invalid_argument("id-list format requires string or utf16");
        if (field.format == FieldFormat::NameRef && field.type != FieldType::U32 && field.type != FieldType::I32)
            throw std::invalid_argument("name-ref format requires u32 or i32");
        if (field.name.empty() || field.name.find_first_of("\t\r\n") != std::string::npos ||
            field.count == 0 || field.count > max_values || field.size > max_values ||
            (field.type == FieldType::PADDING) == (field.size == 0) ||
            (field.type == FieldType::PADDING && field.array) ||
            (field.type == FieldType::GROUP) == field.members.empty() ||
            (!field.array && (field.count != 1 || !field.counter.empty()))) throw std::invalid_argument("Invalid schema field");
        const auto require = [&](const std::string &name)
        {
            if (scope.contains(name) && scope.at(name) == Scoped::NameRef) throw std::invalid_argument(std::format("name-ref field cannot be a counter or condition: {}", name));
            if (!scope.contains(name) || scope.at(name) != Scoped::Integer) throw std::invalid_argument(std::format("Missing preceding integer field: {}", name));
        };
        if (!field.counter.empty()) require(field.counter);
        for (const auto &clause : field.enabled_by)
            for (const auto &term : clause)
                require(term.field);
        if (field.type == FieldType::GROUP) validate_fields(field.members, scope, depth + 1);
        if (!local.insert(field.name).second) continue;
        if (field.format == FieldFormat::NameRef) scope[field.name] = Scoped::NameRef;
        else scope[field.name] = binary::is_integer(field.type) ? Scoped::Integer : Scoped::Other;
    }
}

void validate_schema(const Schema &schema)
{
    if (schema.blocks.empty()) throw std::invalid_argument("Schema has no blocks");
    for (std::size_t i = 0; i < schema.blocks.size(); ++i)
    {
        const auto &block = schema.blocks[i];
        if (block.record_count < Block::eof || block.record_count > std::numeric_limits<std::uint32_t>::max()) throw std::invalid_argument("Invalid record count");
        validate_fields(block.fields);
        if (!block.nonzero.empty())
        {
            const auto field = std::ranges::find(block.fields, block.nonzero, &Field::name);
            if (block.record_count != Block::stored || field == block.fields.end() || !binary::is_integer(field->type)) throw std::invalid_argument("Record counter must name an integer field");
        }
        if (i + 1 < schema.blocks.size() && (block.record_count == Block::eof || !block.nonzero.empty()))
            throw std::invalid_argument("Only the last block may run to the end");
    }
}

bool valid_client_name(std::string_view name)
{
    return !name.empty() && name != "." && name != ".." &&
           std::ranges::all_of(name, [](unsigned char c)
                               { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'; });
}

Schema schema_value(const Json::Value &root)
{
    object(root, {"blocks", "safePackage", "clients"});
    Schema schema;
    if (root.isMember("clients"))
    {
        const auto &clients = root["clients"];
        if (!clients.isArray() || clients.empty()) throw std::invalid_argument("Expected nonempty clients array");
        std::set<std::string> unique;
        for (const auto &client : clients)
        {
            const auto name = string(client);
            if (!valid_client_name(name) || !unique.insert(name).second) throw std::invalid_argument(std::format("Invalid or duplicate client version: {}", name));
            schema.clients.push_back(name);
        }
    }
    if (root.isMember("safePackage")) schema.safe_package = boolean(root["safePackage"]);
    const auto &definitions = at(root, "blocks");
    if (!definitions.isArray() || definitions.empty()) throw std::invalid_argument("Expected nonempty blocks array");
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
                if (mode == "compact") b.record_count = Block::compact;
                else if (mode == "eof") b.record_count = Block::eof;
                else throw std::invalid_argument(std::format("Unknown recordCount mode: {}", mode));
            }
            else if (n.isObject())
            {
                object(n, {"nonzero"});
                b.nonzero = string(at(n, "nonzero"));
                if (b.nonzero.empty()) throw std::invalid_argument("Empty nonzero field");
            }
            else
            {
                b.record_count = integer(n);
                if (b.record_count < 0) throw std::invalid_argument("Fixed recordCount must be nonnegative");
            }
        }
        schema.blocks.push_back(std::move(b));
    }
    validate_schema(schema);
    return schema;
}
} // namespace l2asmdisasm::json
