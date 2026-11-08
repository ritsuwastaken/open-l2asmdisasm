#include "l2asmdisasm_private.h" // IWYU pragma: keep

#include "binary.h"
#include "encoding.h"
#include "json.h"
#include <algorithm>

namespace l2asmdisasm
{
namespace
{
constexpr std::string_view marker("\x0cSafePackage\0", 13);

void validate_names(const NameTable &names)
{
    if (names.size() > UINT32_MAX) util::fail("Too many name table entries");
    for (const auto &name : names)
        encoding::validate_utf8(name.value);
}
}

CompactIndex decode_compact_index(std::span<const std::uint8_t> input)
{
    return binary::decode_compact_index(input);
}

std::vector<std::uint8_t> encode_compact_index(std::int32_t value)
{
    return binary::encode_compact_index(value);
}

NameTable read_name_table(std::span<const std::uint8_t> input)
{
    if (input.size() < 4 + marker.size()) util::fail("Truncated name table");
    if (!std::equal(marker.begin(), marker.end(), input.last(marker.size()).begin())) util::fail("Invalid SafePackage trailer");
    binary::Reader reader{input.first(input.size() - marker.size())};
    const auto count = reader.integer(4);
    if (count > (reader.bytes.size() - reader.offset) / 4) util::fail("Name count exceeds available DAT bytes");
    NameTable names;
    names.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        auto value = binary::read(reader, FieldType::UTF16, 0).text;
        const bool terminated = value.ends_with('\0');
        if (terminated) value.pop_back();
        names.push_back({std::move(value), terminated});
    }
    if (reader.offset != reader.bytes.size()) util::fail("Unexpected trailing name table bytes");
    return names;
}

std::vector<std::uint8_t> write_name_table(const NameTable &names)
{
    validate_names(names);
    std::vector<std::uint8_t> output;
    binary::put(output, static_cast<std::uint32_t>(names.size()), 4);
    for (const auto &name : names)
        binary::write(output, FieldType::UTF16, {name.value + (name.terminated ? std::string(1, '\0') : std::string{}), true}, 0);
    output.insert(output.end(), marker.begin(), marker.end());
    return output;
}

std::string disassemble(std::span<const std::uint8_t> input, const Schema &schema, const NameTable *names)
{
    using namespace json;
    if (names) validate_names(*names);
    return checked_json([&]
                        {
        validate_schema(schema);
        const auto &parts = schema.blocks;
        const auto plans = record_plans(parts);
        const auto tail = schema.trailer ? marker.size() : 0;
        if (input.size() < tail) util::fail("Truncated DAT trailer");
        if (schema.trailer && !std::equal(marker.begin(), marker.end(), input.last(tail).begin())) util::fail("Invalid SafePackage trailer");
        binary::Reader reader{input.first(input.size() - tail)};
        Records walker{&reader, nullptr};
        walker.names = names;
        auto output = array();
        for (std::size_t b = 0; b < parts.size(); ++b)
        {
            const auto &block = parts[b];
            const bool open = block.record_count == Block::to_end || !block.counted_by.empty();
            std::int64_t stored = block.record_count;
            if (stored == Block::stored) stored = reader.integer(4);
            else if (stored == Block::compact)
            {
                stored = reader.compact();
                if (stored < 0) util::fail("Negative compact record count");
            }
            auto records = array();
            std::uint64_t flagged = 0;
            for (std::uint64_t row = 0; open ? reader.offset < reader.bytes.size() : row < std::uint64_t(stored); ++row)
            {
                JsonValue record;
                walker.row(*plans[b], record, b, row);
                if (!block.counted_by.empty() && plans[b]->get(block.counted_by) != 0) ++flagged;
                records.append(std::move(record));
            }
            if (!block.counted_by.empty() && flagged != std::uint64_t(stored)) util::fail("Stored record count differs from nonzero count");
            output.append(std::move(records));
        }
        if (reader.offset != reader.bytes.size()) util::fail("Unexpected trailing DAT bytes");
        return write_document(output, plans); });
}

std::vector<std::uint8_t> assemble(std::string_view input, const Schema &schema, NameTable *names)
{
    using namespace json;
    if (names) validate_names(*names);
    auto updated = names ? std::optional<NameTable>(*names) : std::nullopt;
    auto result = checked_json([&]
                        {
        validate_schema(schema);
        const auto &parts = schema.blocks;
        const auto plans = record_plans(parts);
        auto document = parse_json(input);
        auto data = array();
        if (parts.size() == 1) data.append(std::move(document));
        else
        {
            object(document, {"blocks"});
            (void)at(document, "blocks");
            data = std::move(document["blocks"]);
        }
        if (!data.isArray() || data.size() != parts.size()) util::fail("JSON block count differs from schema");
        std::vector<std::uint8_t> output;
        Records walker{nullptr, &output, input};
        walker.updated_names = updated ? &*updated : nullptr;
        if (updated)
            for (std::size_t i = 0; i < updated->size(); ++i)
                walker.name_indexes.try_emplace((*updated)[i].value, static_cast<std::uint32_t>(i));
        for (std::size_t b = 0; b < parts.size(); ++b)
        {
            const auto &block = parts[b];
            auto &records = data[static_cast<Json::ArrayIndex>(b)];
            if (!records.isArray()) util::fail("Expected array of records");
            const auto n = records.size();
            if (block.record_count >= 0 && n != std::uint64_t(block.record_count)) util::fail("JSON record count differs from fixed recordCount");
            const auto start = output.size();
            if (block.record_count == Block::stored) binary::put(output, n, 4);
            else if (block.record_count == Block::compact)
            {
                if (n > INT32_MAX) util::fail("Too many compact-counted records");
                const auto count = binary::encode_compact_index(static_cast<std::int32_t>(n));
                output.insert(output.end(), count.begin(), count.end());
            }
            std::uint32_t flagged = 0;
            for (Json::ArrayIndex row = 0; row < n; ++row)
            {
                auto &record = records[row];
                walker.row(*plans[b], record, b, row);
                if (!block.counted_by.empty() && plans[b]->get(block.counted_by) != 0) ++flagged;
            }
            if (!block.counted_by.empty())
                for (unsigned byte = 0; byte < 4; ++byte)
                    output[start + byte] = static_cast<std::uint8_t>(flagged >> (byte * 8));
        }
        if (schema.trailer) output.insert(output.end(), marker.begin(), marker.end());
        return output; });
    if (names) *names = std::move(*updated);
    return result;
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

Schema parse_schema(std::string_view input, std::string_view client_version)
{
    return json::parse_schema(input, client_version);
}

std::vector<Schema> parse_schemas(std::string_view input)
{
    return json::parse_schemas(input);
}

std::string export_schema(const Schema &schema)
{
    return json::export_schema(schema);
}

std::string export_schemas(std::span<const Schema> schemas)
{
    return json::export_schemas(schemas);
}

std::filesystem::path resolve_definition(const std::filesystem::path &directory, std::string_view filename)
{
    return json::resolve_definition(directory, filename);
}
} // namespace l2asmdisasm
