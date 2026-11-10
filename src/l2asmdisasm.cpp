#include "l2asmdisasm_private.h" // IWYU pragma: keep

#include "binary.h"
#include "json_writer.h"
#include <algorithm>
#include <format>
#include <set>
#include <utility>

namespace l2asmdisasm
{
namespace
{
constexpr std::string_view safe_package_marker("\x0cSafePackage\0", 13);

std::span<const std::uint8_t> strip_marker(std::span<const std::uint8_t> input)
{
    if (!std::ranges::equal(safe_package_marker, input.last(safe_package_marker.size()))) throw std::invalid_argument("Invalid SafePackage marker");
    return input.first(input.size() - safe_package_marker.size());
}

std::uint64_t read_record_count(binary::Reader &reader, std::int64_t mode)
{
    if (mode == Block::stored) return reader.integer(4);
    if (mode != Block::compact) return mode == Block::eof ? 0 : static_cast<std::uint64_t>(mode);
    const auto n = reader.compact();
    if (n < 0) throw std::invalid_argument("Negative compact record count");
    return static_cast<std::uint64_t>(n);
}

void read_block(json::RecordWalker &walker, const Block &block, json::RecordPlan &plan, std::size_t b, json::Writer &writer)
{
    auto &reader = *walker.reader;
    const bool open = block.record_count == Block::eof || !block.nonzero.empty();
    const auto stored = read_record_count(reader, block.record_count);
    std::uint64_t flagged = 0;
    writer.begin_block();
    for (std::uint64_t row = 0; open ? reader.offset < reader.bytes.size() : row < stored; ++row)
    {
        Json::Value record;
        walker.row(plan, record, b, row);
        if (!block.nonzero.empty() && plan.get(block.nonzero) != 0) ++flagged;
        writer.record(record, plan);
    }
    if (!block.nonzero.empty() && flagged != stored) throw std::invalid_argument("Stored record count differs from nonzero count");
    writer.end_block();
}

void write_block(json::RecordWalker &walker, const Block &block, json::RecordPlan &plan, Json::Value &records, std::size_t b)
{
    auto &output = *walker.output;
    if (!records.isArray()) throw std::invalid_argument("Expected array of records");
    const auto n = records.size();
    if (block.record_count >= 0 && n != static_cast<std::uint64_t>(block.record_count)) throw std::invalid_argument("JSON record count differs from fixed recordCount");
    const auto start = output.size();
    if (block.record_count == Block::stored) binary::put(output, n, 4);
    else if (block.record_count == Block::compact)
    {
        if (!std::in_range<std::int32_t>(n)) throw std::invalid_argument("Too many compact-counted records");
        binary::put_compact(output, static_cast<std::int32_t>(n));
    }
    std::uint64_t flagged = 0;
    for (Json::ArrayIndex row = 0; row < n; ++row)
    {
        walker.row(plan, records[row], b, row);
        if (!block.nonzero.empty() && plan.get(block.nonzero) != 0) ++flagged;
    }
    if (!block.nonzero.empty())
        for (unsigned byte = 0; byte < 4; ++byte)
            output[start + byte] = static_cast<std::uint8_t>(flagged >> (byte * 8));
}
} // namespace

NameTable read_name_table(std::span<const std::uint8_t> input)
{
    if (input.size() < 4 + safe_package_marker.size()) throw std::invalid_argument("Truncated name table");
    binary::Reader reader{strip_marker(input)};
    const auto count = reader.integer(4);
    if (count > (reader.bytes.size() - reader.offset) / 4) throw std::invalid_argument("Name count exceeds available DAT bytes");
    NameTable names;
    names.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
        names.push_back(binary::read(reader, FieldType::UTF16, 0).text);
    if (reader.offset != reader.bytes.size()) throw std::invalid_argument("Unexpected trailing name table bytes");
    return names;
}

std::vector<std::uint8_t> write_name_table(const NameTable &names)
{
    if (!std::in_range<std::uint32_t>(names.size())) throw std::invalid_argument("Too many name table entries");
    std::vector<std::uint8_t> output;
    binary::put(output, names.size(), 4);
    for (const auto &name : names)
        binary::write(output, FieldType::UTF16, {.text = name, .wide = true}, 0);
    output.insert(output.end(), safe_package_marker.begin(), safe_package_marker.end());
    return output;
}

std::string disassemble(std::span<const std::uint8_t> input, const Schema &schema, const NameTable *names)
{
    json::validate_schema(schema);
    const auto plans = json::record_plans(schema.blocks);
    if (schema.safe_package)
    {
        if (input.size() < safe_package_marker.size()) throw std::invalid_argument("Truncated SafePackage marker");
        input = strip_marker(input);
    }
    binary::Reader reader{input};
    json::RecordWalker walker{.reader = &reader, .names = names};
    json::Writer writer(schema.blocks.size());
    for (std::size_t b = 0; b < schema.blocks.size(); ++b)
        read_block(walker, schema.blocks[b], *plans[b], b, writer);
    if (reader.offset != reader.bytes.size()) throw std::invalid_argument("Unexpected trailing DAT bytes");
    return writer.finish();
}

std::vector<std::uint8_t> assemble(std::string_view input, const Schema &schema, NameTable *names)
{
    const auto size = names ? names->size() : 0;
    try
    {
        json::validate_schema(schema);
        const auto plans = json::record_plans(schema.blocks);
        auto document = json::parse_json(input);
        auto data = json::array();
        if (schema.blocks.size() == 1) data.append(std::move(document));
        else
        {
            json::object(document, {"blocks"});
            (void)json::at(document, "blocks");
            data = std::move(document["blocks"]);
        }
        if (!data.isArray() || data.size() != schema.blocks.size()) throw std::invalid_argument("JSON block count differs from schema");
        std::vector<std::uint8_t> output;
        json::RecordWalker walker{.output = &output, .source = input, .lookup = {.names = names}};
        for (std::size_t b = 0; b < schema.blocks.size(); ++b)
            write_block(walker, schema.blocks[b], *plans[b], data[static_cast<Json::ArrayIndex>(b)], b);
        if (schema.safe_package) output.insert(output.end(), safe_package_marker.begin(), safe_package_marker.end());
        return output;
    }
    catch (...)
    {
        if (names) names->resize(size);
        throw;
    }
}

std::string disassemble(std::span<const std::uint8_t> input, std::string_view definition, std::string_view client_version,
                        std::span<const std::uint8_t> names)
{
    const auto schema = parse_schema(definition, client_version);
    if (names.empty()) return disassemble(input, schema);
    const auto table = read_name_table(names);
    return disassemble(input, schema, &table);
}

std::vector<std::uint8_t> assemble(std::string_view input, std::string_view definition, std::string_view client_version,
                                   std::vector<std::uint8_t> *names)
{
    const auto schema = parse_schema(definition, client_version);
    if (!names) return assemble(input, schema);
    auto table = read_name_table(*names);
    const auto count = table.size();
    auto output = assemble(input, schema, &table);
    if (table.size() != count) *names = write_name_table(table);
    return output;
}

std::vector<Schema> parse_schemas(std::string_view text)
{
    const auto root = json::parse_json(text);
    std::vector<Schema> schemas;
    if (!root.isArray())
    {
        schemas.push_back(json::schema_value(root));
        return schemas;
    }
    if (root.empty()) throw std::invalid_argument("Expected nonempty schema array");
    std::set<std::string> clients;
    for (const auto &value : root)
    {
        auto schema = json::schema_value(value);
        if (schema.clients.empty()) throw std::invalid_argument("Versioned layouts require clients");
        for (const auto &client : schema.clients)
            if (!clients.insert(client).second) throw std::invalid_argument(std::format("Ambiguous definition for client version {}", client));
        schemas.push_back(std::move(schema));
    }
    return schemas;
}

Schema parse_schema(std::string_view text, std::string_view client_version)
{
    auto schemas = parse_schemas(text);
    if (schemas.size() == 1 && (client_version.empty() || schemas.front().clients.empty())) return std::move(schemas.front());
    for (auto &schema : schemas)
        if (std::ranges::find(schema.clients, client_version) != schema.clients.end()) return std::move(schema);
    std::set<std::string> available;
    for (const auto &schema : schemas)
        available.insert(schema.clients.begin(), schema.clients.end());
    std::string list;
    for (const auto &client : available)
        list += (list.empty() ? "" : ", ") + client;
    if (client_version.empty()) throw std::invalid_argument(std::format("Client version required for multiple layouts; available: {}", list));
    throw std::invalid_argument(std::format("No definition for client version {}; available: {}", client_version, list));
}
} // namespace l2asmdisasm
