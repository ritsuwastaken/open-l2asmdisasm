#include "l2asmdisasm_private.h" // IWYU pragma: keep

#include "binary.h"
#include "encoding.h"
#include "json.h"
#include <algorithm>

namespace l2asmdisasm
{
namespace
{
constexpr std::string_view safe_package_marker("\x0cSafePackage\0", 13);
}

NameTable read_name_table(std::span<const std::uint8_t> input)
{
    if (input.size() < 4 + safe_package_marker.size()) util::fail("Truncated name table");
    if (!std::equal(safe_package_marker.begin(), safe_package_marker.end(), input.last(safe_package_marker.size()).begin())) util::fail("Invalid SafePackage marker");
    binary::Reader reader{input.first(input.size() - safe_package_marker.size())};
    const auto count = reader.integer(4);
    if (count > (reader.bytes.size() - reader.offset) / 4) util::fail("Name count exceeds available DAT bytes");
    NameTable names;
    names.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
        names.push_back(binary::read(reader, FieldType::UTF16, 0).text);
    if (reader.offset != reader.bytes.size()) util::fail("Unexpected trailing name table bytes");
    return names;
}

std::vector<std::uint8_t> write_name_table(const NameTable &names)
{
    if (names.size() > UINT32_MAX) util::fail("Too many name table entries");
    std::vector<std::uint8_t> output;
    binary::put(output, static_cast<std::uint32_t>(names.size()), 4);
    for (const auto &name : names)
    {
        encoding::validate_utf8(name);
        binary::write(output, FieldType::UTF16, {name, true}, 0);
    }
    output.insert(output.end(), safe_package_marker.begin(), safe_package_marker.end());
    return output;
}

std::string disassemble(std::span<const std::uint8_t> input, const Schema &schema, const NameTable *names)
{
    using namespace json;
    return checked_json([&]
                        {
        validate_schema(schema);
        const auto &blocks = schema.blocks;
        const auto plans = record_plans(blocks);
        const auto tail = schema.safe_package ? safe_package_marker.size() : 0;
        if (input.size() < tail) util::fail("Truncated SafePackage marker");
        if (schema.safe_package && !std::equal(safe_package_marker.begin(), safe_package_marker.end(), input.last(tail).begin())) util::fail("Invalid SafePackage marker");
        binary::Reader reader{input.first(input.size() - tail)};
        RecordWalker walker{&reader, nullptr};
        walker.names = names;
        auto output = array();
        for (std::size_t b = 0; b < blocks.size(); ++b)
        {
            const auto &block = blocks[b];
            const bool open = block.record_count == Block::eof || !block.nonzero.empty();
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
                Json::Value record;
                walker.row(*plans[b], record, b, row);
                if (!block.nonzero.empty() && plans[b]->get(block.nonzero) != 0) ++flagged;
                records.append(std::move(record));
            }
            if (!block.nonzero.empty() && flagged != std::uint64_t(stored)) util::fail("Stored record count differs from nonzero count");
            output.append(std::move(records));
        }
        if (reader.offset != reader.bytes.size()) util::fail("Unexpected trailing DAT bytes");
        return write_document(output, plans); });
}

std::vector<std::uint8_t> assemble(std::string_view input, const Schema &schema, NameTable *names)
{
    using namespace json;
    struct Rollback
    {
        NameTable *names;
        std::size_t size;
        ~Rollback()
        {
            if (names) names->resize(size);
        }
    } rollback{names, names ? names->size() : 0};
    auto result = checked_json([&]
                               {
        validate_schema(schema);
        const auto &blocks = schema.blocks;
        const auto plans = record_plans(blocks);
        auto document = parse_json(input);
        auto data = array();
        if (blocks.size() == 1) data.append(std::move(document));
        else
        {
            object(document, {"blocks"});
            (void)at(document, "blocks");
            data = std::move(document["blocks"]);
        }
        if (!data.isArray() || data.size() != blocks.size()) util::fail("JSON block count differs from schema");
        std::vector<std::uint8_t> output;
        RecordWalker walker{nullptr, &output, input};
        walker.lookup.names = names;
        for (std::size_t b = 0; b < blocks.size(); ++b)
        {
            const auto &block = blocks[b];
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
                if (!block.nonzero.empty() && plans[b]->get(block.nonzero) != 0) ++flagged;
            }
            if (!block.nonzero.empty())
                for (unsigned byte = 0; byte < 4; ++byte)
                    output[start + byte] = static_cast<std::uint8_t>(flagged >> (byte * 8));
        }
        if (schema.safe_package) output.insert(output.end(), safe_package_marker.begin(), safe_package_marker.end());
        return output; });
    rollback.names = nullptr;
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
} // namespace l2asmdisasm
