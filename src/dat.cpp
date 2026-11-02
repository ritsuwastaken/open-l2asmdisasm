#include "dat.h"
#include "binary.h"
#include "encoding.h"
#include "layout.h"
#include "util.h"
#include "value.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <optional>

namespace dat
{
using namespace l2asmdisasm;

namespace
{
using namespace layout;

constexpr std::string_view marker("\x0cSafePackage\0", 13);
constexpr std::string_view block_marker = "\\block"; // not a valid escaped text value

template <typename Prefix, typename Body>
auto in_context(Prefix prefix, Body body)
{
    try
    {
        return body();
    }
    catch (const std::invalid_argument &error)
    {
        util::fail(prefix() + error.what());
    }
}

std::vector<std::size_t> display_order(const Schema &schema, const Walker &layout)
{
    const auto order = [&](std::size_t column)
    { return schema.fields[std::upper_bound(layout.ends.begin(), layout.ends.end(), column) - layout.ends.begin()].order; };
    std::vector<std::size_t> display;
    for (std::size_t i = 0; i < layout.columns; ++i)
        if ((schema.ignore_order || order(i) != -1) && !std::binary_search(layout.hidden.begin(), layout.hidden.end(), i)) display.push_back(i);
    if (!schema.ignore_order)
        std::stable_sort(display.begin(), display.end(), [&](auto a, auto b)
                         { return order(a) && (!order(b) || *order(a) < *order(b)); });
    return display;
}

bool skipped(const Field &field, const Field::Skip &rule, const value::Value &cell)
{
    const auto bits = static_cast<std::uint32_t>(cell.integer);
    if (field.type == FieldType::FLOAT) return !value::is_nan(bits) && std::bit_cast<float>(bits) >= rule.low && std::bit_cast<float>(bits) <= rule.high;
    const auto size = field.type == FieldType::CNTR ? 4 : util::min_size(field.type);
    const std::int32_t number = size == 4 ? std::bit_cast<std::int32_t>(bits) : size == 2 ? std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(bits))
                                                                                          : std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(bits));
    const auto bound = [](double v)
    { return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int64_t>(v))); };
    return number >= bound(rule.low) && number <= bound(rule.high);
}

void apply_skipif(const Schema &schema, const std::vector<std::size_t> &ends, std::vector<Cell> &cells)
{
    for (std::size_t f = 0, start = 0; f < schema.fields.size(); start = ends[f++])
        for (const auto &rule : schema.fields[f].skip_if)
            for (auto i = start + rule.first; i < ends[f] && i <= start + rule.last; ++i)
                if (cells[i] && skipped(schema.fields[f], rule, *cells[i])) cells[i].reset();
}

template <typename Cells>
void line(std::string &output, const Cells &cells, const std::vector<std::size_t> &display, std::string_view separator)
{
    for (std::size_t i = 0; i < display.size(); ++i)
        output.append(i ? separator : "").append(cells[display[i]]);
    output += '\n';
}

std::vector<std::string_view> next_row(std::string_view &text)
{
    const auto end = text.find('\n');
    auto line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    if (line.ends_with('\r')) line.remove_suffix(1);
    std::vector<std::string_view> cells;
    for (std::size_t tab = 0; tab != std::string_view::npos; line.remove_prefix(tab + 1))
        cells.push_back(line.substr(0, tab = line.find('\t')));
    return cells;
}

std::vector<Schema> split(const Schema &schema)
{
    std::vector<Schema> parts;
    for (std::size_t b = 0; b <= schema.blocks.size(); ++b)
    {
        auto &part = parts.emplace_back(schema);
        if (b) static_cast<Block &>(part) = schema.blocks[b - 1];
        part.blocks.clear();
    }
    for (std::size_t b = 0; b < parts.size(); ++b)
    {
        validate(parts[b], Index(parts[b]));
        const bool open = parts[b].record_count == Block::to_end || !parts[b].counted_by.empty();
        if (open && b + 1 < parts.size()) util::fail("Only the last block may run to the end");
    }
    if (parts.size() > 1 && !schema.header) util::fail("Several blocks require HEADER");
    return parts;
}

} // namespace

std::string disassemble(std::span<const std::uint8_t> input, const Schema &definition, Schema *sized_schema, const TextOptions &options)
{
    const encoding::Conversion conversion(options);
    auto parts = split(definition);
    std::vector<Index> indexes;
    for (const auto &part : parts)
        indexes.emplace_back(part);
    binary::Reader reader{input};
    const auto tail = definition.magic * 4 + (definition.trailer ? marker.size() : 0);
    const auto read = [&](FieldType type, const Name &name, bool active, std::size_t size) -> Cell
    {
        if (!active) return std::nullopt;
        const auto offset = reader.offset;
        const auto where = [&]
        { return "field " + name.str() + ", byte " + std::to_string(offset) + ": "; };
        return in_context(where, [&]
                          { return binary::read(reader, type, size, conversion); });
    };

    std::vector<std::size_t> starts;
    std::vector<std::uint32_t> counts;
    for (std::size_t b = 0; b < parts.size(); ++b)
    {
        auto &schema = parts[b];
        const auto &index = indexes[b];
        const bool counted = !schema.counted_by.empty();
        const bool open = counted || schema.record_count == Block::to_end;
        std::uint32_t stored = 0;
        if (schema.record_count == Block::stored) stored = reader.integer(4);
        else if (schema.record_count == Block::compact)
        {
            const auto offset = reader.offset;
            const auto count = in_context([&]
                                          { return "Record count, byte " + std::to_string(offset) + ": "; }, [&]
                                          { return reader.compact(); });
            if (count < 0) util::fail("Negative compact record count");
            stored = static_cast<std::uint32_t>(count);
        }
        else if (!open) stored = static_cast<std::uint32_t>(schema.record_count);
        starts.push_back(reader.offset);
        std::size_t minimum_size = 0;
        for (const auto &field : schema.fields)
            if (field.counter.empty() && field.enabled_by.empty()) minimum_size += util::min_size(field.type) * field.count;
        if (minimum_size == 0 || (!open && stored > (input.size() - reader.offset) / minimum_size)) util::fail("Record count exceeds available DAT bytes");

        Walker scan{schema, index, Pass::Scan, read};
        std::uint32_t count = 0, flagged = 0;
        for (; open ? input.size() > reader.offset + tail : count < stored; ++count)
        {
            const auto where = [&]
            { return (parts.size() > 1 ? "Block " + std::to_string(b + 1) + ", record " : "Record ") + std::to_string(count + 1) + ", "; };
            in_context(where, [&]
                       { scan.record(); });
            if (counted && scan.values[index.at(schema.counted_by)] != 0) ++flagged;
        }
        if (counted && flagged != stored)
            util::fail("Stored record count " + std::to_string(stored) + " differs from the " + std::to_string(flagged) + " records with nonzero " + schema.counted_by);
        counts.push_back(count);
    }
    for (std::size_t i = 0; i < definition.magic; ++i)
        if (reader.integer(4) != 0) util::fail("Nonzero MAGIC dword at byte " + std::to_string(reader.offset - 4));
    if (definition.trailer)
    {
        const auto trailer = reader.take(marker.size());
        if (!std::equal(trailer.begin(), trailer.end(), marker.begin()) || reader.offset != input.size())
            util::fail("Invalid SafePackage trailer or unexpected trailing bytes at byte " + std::to_string(reader.offset - marker.size()));
    }
    else if (reader.offset != input.size()) util::fail("Unexpected trailing bytes at byte " + std::to_string(reader.offset));

    std::string output;
    for (std::size_t b = 0; b < parts.size(); ++b)
    {
        auto &schema = parts[b];
        std::vector<std::string> names;
        const auto display = display_order(schema, header(schema, indexes[b], names));
        if (b) output.append(block_marker).append("\n");
        if (schema.header) line(output, names, display, schema.separator);
        std::vector<Cell> cells;
        std::vector<std::string> texts;
        std::vector<FieldType> types;
        Walker format{schema, indexes[b], Pass::Layout, [&](FieldType type, const Name &name, bool active, std::size_t size)
                      {
                          types.push_back(type);
                          return cells.emplace_back(read(type, name, active, size));
                      }};
        reader.offset = starts[b];
        for (std::uint32_t row = 0; row < counts[b]; ++row)
        {
            cells.clear();
            types.clear();
            texts.clear();
            format.record();
            apply_skipif(schema, format.ends, cells);
            for (std::size_t i = 0; i < cells.size(); ++i)
                texts.push_back(cells[i] ? value::format(*cells[i], types[i], options) : std::string{});
            line(output, texts, display, schema.separator);
        }
    }
    if (sized_schema)
    {
        *sized_schema = definition;
        sized_schema->fields = parts[0].fields;
        for (std::size_t b = 1; b < parts.size(); ++b)
            sized_schema->blocks[b - 1].fields = parts[b].fields;
    }
    return output;
}

std::vector<std::uint8_t> assemble(std::string_view text, const Schema &definition, const TextOptions &options)
{
    const encoding::Conversion conversion(options);
    auto parts = split(definition);
    std::vector<Index> indexes;
    std::vector<std::vector<std::string>> headers;
    for (auto &part : parts)
    {
        const auto &index = indexes.emplace_back(part);
        (void)header(part, index, headers.emplace_back());
    }
    std::vector<std::uint8_t> output, body;
    std::size_t line_number = 0, column = 0, b = 0;
    bool expect_header = definition.header;
    std::uint64_t records = 0, flagged = 0;
    std::vector<std::string_view> cells;
    const auto finish = [&]
    {
        const auto &schema = parts[b];
        if (!schema.counted_by.empty()) records = flagged;
        if (schema.record_count == Block::stored) binary::put(output, static_cast<std::uint32_t>(records), 4);
        else if (schema.record_count == Block::compact)
        {
            if (records > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) util::fail("Too many records");
            const auto count = binary::encode_compact_index(static_cast<std::int32_t>(records));
            output.insert(output.end(), count.begin(), count.end());
        }
        else if (schema.record_count >= 0 && records != static_cast<std::uint64_t>(schema.record_count))
            util::fail("Text record count differs from RECCNT");
        output.insert(output.end(), body.begin(), body.end());
        body.clear();
        records = flagged = 0;
    };
    const auto write = [&](FieldType type, const Name &name, bool active, std::size_t size)
    {
        const auto cell_text = cells[column++];
        const auto where = [&]
        { return "Text line " + std::to_string(line_number) + ", field " + name.str() + ": "; };
        if (!active && !cell_text.empty()) util::fail(where() + "Nonempty padding or disabled field");
        if (!active) return Cell{};
        return in_context(where, [&]
                          {
                              auto cell = value::parse(cell_text, type, options);
                              binary::write(body, type, cell, size, conversion);
                              return Cell(std::move(cell)); });
    };
    std::vector<Walker> writers;
    for (std::size_t i = 0; i < parts.size(); ++i)
        writers.push_back(Walker{parts[i], indexes[i], Pass::Layout, write});
    while (!text.empty())
    {
        cells = next_row(text);
        ++line_number;
        const auto is_header = [&](std::size_t part)
        { return cells.size() == headers[part].size() && std::equal(cells.begin(), cells.end(), headers[part].begin()); };
        if (expect_header)
        {
            if (!is_header(b)) util::fail("Text header does not match DDF columns");
            expect_header = false;
            continue;
        }
        if (parts.size() > 1 && cells.size() == 1 && cells[0] == block_marker)
        {
            if (b + 1 == parts.size()) util::fail("Unexpected text block marker");
            finish();
            ++b;
            expect_header = true;
            continue;
        }
        if (cells.size() != headers[b].size()) util::fail("Text line " + std::to_string(line_number) + ": expected " + std::to_string(headers[b].size()) + " columns, got " + std::to_string(cells.size()));
        if (++records > std::numeric_limits<std::uint32_t>::max()) util::fail("Too many records");
        column = 0;
        writers[b].record();
        if (!parts[b].counted_by.empty() && writers[b].values[indexes[b].at(parts[b].counted_by)] != 0) ++flagged;
    }
    if (definition.header && line_number == 0) util::fail("Missing text header");
    if (expect_header) util::fail("Missing text header of block " + std::to_string(b + 1));
    if (b + 1 != parts.size()) util::fail("Missing text header of block " + std::to_string(b + 2));
    finish();
    for (std::size_t i = 0; i < definition.magic; ++i)
        binary::put(output, 0, 4);
    if (definition.trailer) output.insert(output.end(), marker.begin(), marker.end());
    return output;
}
} // namespace dat
