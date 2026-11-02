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
    const auto size = field.type == FieldType::CNTR ? 4 : util::width(field.type);
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
} // namespace

std::string disassemble(std::span<const std::uint8_t> input, const Schema &definition, Schema *sized_schema, const TextOptions &options)
{
    const encoding::Conversion conversion(options);
    auto schema = definition;
    const Index index(schema);
    validate(schema, index);
    binary::Reader reader{input};
    const bool counted = !schema.counted_by.empty();
    const std::uint32_t stored = schema.record_count == -1 ? reader.integer(4) : static_cast<std::uint32_t>(schema.record_count);
    const auto start = reader.offset;
    const auto tail = schema.magic * 4 + marker.size();
    std::size_t minimum_size = 0;
    for (const auto &field : schema.fields)
        if (field.counter.empty() && field.enabled_by.empty()) minimum_size += util::width(field.type) * field.count;
    if (minimum_size == 0 || (!counted && stored > (input.size() - reader.offset) / minimum_size)) util::fail("Record count exceeds available DAT bytes");
    const auto read = [&](FieldType type, const Name &name, bool active, std::size_t size) -> Cell
    {
        if (!active) return std::nullopt;
        const auto offset = reader.offset;
        const auto where = [&]
        { return "field " + name.str() + ", byte " + std::to_string(offset) + ": "; };
        return in_context(where, [&]
                          { return binary::read(reader, type, size, conversion); });
    };

    Walker scan{schema, index, Pass::Scan, read};
    std::uint32_t count = 0, flagged = 0;
    for (; counted ? input.size() > reader.offset + tail : count < stored; ++count)
    {
        const auto where = [&]
        { return "Record " + std::to_string(count + 1) + ", "; };
        in_context(where, [&]
                   { scan.record(); });
        if (counted && scan.values[index.at(schema.counted_by)] != 0) ++flagged;
    }
    if (counted && flagged != stored)
        util::fail("Stored record count " + std::to_string(stored) + " differs from the " + std::to_string(flagged) + " records with nonzero " + schema.counted_by);
    for (std::size_t i = 0; i < schema.magic; ++i)
        if (reader.integer(4) != 0) util::fail("Nonzero MAGIC dword at byte " + std::to_string(reader.offset - 4));
    const auto trailer = reader.take(marker.size());
    if (!std::equal(trailer.begin(), trailer.end(), marker.begin()) || reader.offset != input.size())
        util::fail("Invalid SafePackage trailer or unexpected trailing bytes at byte " + std::to_string(reader.offset - marker.size()));

    std::vector<std::string> names;
    const auto display = display_order(schema, header(schema, index, names));
    std::string output;
    if (schema.header) line(output, names, display, schema.separator);
    std::vector<Cell> cells;
    std::vector<std::string> texts;
    std::vector<FieldType> types;
    Walker format{schema, index, Pass::Layout, [&](FieldType type, const Name &name, bool active, std::size_t size)
                  {
                      types.push_back(type);
                      return cells.emplace_back(read(type, name, active, size));
                  }};
    reader.offset = start;
    for (std::uint32_t row = 0; row < count; ++row)
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
    if (sized_schema) *sized_schema = std::move(schema);
    return output;
}

std::vector<std::uint8_t> assemble(std::string_view text, const Schema &definition, const TextOptions &options)
{
    const encoding::Conversion conversion(options);
    auto schema = definition;
    const Index index(schema);
    validate(schema, index);
    std::vector<std::string> names;
    (void)header(schema, index, names);
    std::vector<std::uint8_t> output;
    if (schema.record_count == -1) binary::put(output, 0, 4);
    std::size_t line_number = 0, column = 0;
    std::uint64_t records = 0, flagged = 0;
    std::vector<std::string_view> cells;
    Walker write{schema, index, Pass::Layout, [&](FieldType type, const Name &name, bool active, std::size_t size)
                 {
                     const auto text = cells[column++];
                     const auto where = [&]
                     { return "Text line " + std::to_string(line_number) + ", field " + name.str() + ": "; };
                     if (!active && !text.empty()) util::fail(where() + "Nonempty padding or disabled field");
                     if (!active) return Cell{};
                     return in_context(where, [&]
                                       {
                                           auto cell = value::parse(text, type, options);
                                           binary::write(output, type, cell, size, conversion);
                                           return Cell(std::move(cell)); });
                 }};
    while (!text.empty())
    {
        cells = next_row(text);
        if (++line_number, cells.size() != names.size()) util::fail("Text line " + std::to_string(line_number) + ": expected " + std::to_string(names.size()) + " columns, got " + std::to_string(cells.size()));
        if (schema.header && line_number == 1)
        {
            if (!std::equal(cells.begin(), cells.end(), names.begin())) util::fail("Text header does not match DDF columns");
            continue;
        }
        if (++records > std::numeric_limits<std::uint32_t>::max()) util::fail("Too many records");
        column = 0;
        write.record();
        if (!schema.counted_by.empty() && write.values[index.at(schema.counted_by)] != 0) ++flagged;
    }
    if (schema.header && line_number == 0) util::fail("Missing text header");
    if (!schema.counted_by.empty()) records = flagged;
    if (schema.record_count == -1)
        for (std::size_t i = 0; i < 4; ++i)
            output[i] = static_cast<std::uint8_t>(records >> (i * 8));
    else if (records != static_cast<std::uint64_t>(schema.record_count)) util::fail("Text record count differs from RECCNT");
    for (std::size_t i = 0; i < schema.magic; ++i)
        binary::put(output, 0, 4);
    output.insert(output.end(), marker.begin(), marker.end());
    return output;
}
} // namespace dat
