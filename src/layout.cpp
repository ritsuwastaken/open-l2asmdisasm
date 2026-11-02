#include "layout.h"
#include "util.h"
#include <algorithm>
#include <limits>
#include <span>

namespace layout
{
namespace
{
std::size_t checked_count(std::int64_t n)
{
    if (n < 0 || n > static_cast<std::int64_t>(limit)) util::fail("Array count must be in [0, 100000]");
    return static_cast<std::size_t>(n);
}

struct Part
{
    FieldType type;
    const char *prefix, *tail;
};

std::span<const Part> parts(FieldType type, bool mesh)
{
    static constexpr Part mat[] = {{FieldType::INT, "_m", "_id"}, {FieldType::INT, "_m", "_cnt"}};
    static constexpr Part mtx[] = {{FieldType::UNICODE, "_m", ""}};
    static constexpr Part mtx2[] = {{FieldType::UNICODE, "_mU", ""}, {FieldType::UCHAR, "_mB", ""}, {FieldType::UINT, "_mD", ""}};
    static constexpr Part mtx3[] = {{FieldType::UNICODE, "_mU", ""}, {FieldType::UCHAR, "_mB", "[1]"}, {FieldType::UCHAR, "_mB", "[2]"}};
    static constexpr Part texture[] = {{FieldType::UNICODE, "_t", ""}};
    if (type >= FieldType::MAT) return mat;
    if (!mesh) return texture;
    if (type == FieldType::MTX) return mtx;
    if (type == FieldType::MTX2) return mtx2;
    return mtx3;
}
} // namespace

std::string Name::str() const
{
    auto text = field.name;
    if (field.type == FieldType::FILLER) text += "{" + std::to_string(field.count) + "}";
    else if (indexed) text += "[" + std::to_string(element) + "]";
    text += suffix;
    if (entry != std::string::npos) text += "[" + std::to_string(entry) + "]" + tail;
    return text;
}

Index::Index(const Schema &schema) : primary(schema.fields.size())
{
    for (std::size_t i = 0; i < schema.fields.size(); ++i)
        primary[i] = first.try_emplace(schema.fields[i].name, i).second;
}

std::size_t Index::at(const std::string &name) const
{
    const auto found = first.find(name);
    if (found == first.end()) util::fail("Missing preceding field '" + name + "'");
    return found->second;
}

void validate(const Schema &schema, const Index &index)
{
    if (schema.separator.empty() || schema.record_count < Block::to_end || schema.record_count > std::numeric_limits<std::uint32_t>::max() || schema.magic > limit)
        util::fail("Invalid or unsupported schema settings");
    if (schema.fields.empty()) util::fail("Schema has no fields");
    if (!schema.counted_by.empty() && (schema.record_count != -1 || !util::is_integer(schema.fields[index.at(schema.counted_by)].type)))
        util::fail("RECCNT must name an integer field");
    for (const auto &field : schema.fields)
    {
        (void)util::width(field.type);
        if (field.name.empty() || field.name.find_first_of("\t\r\n") != std::string::npos ||
            field.count == 0 || field.count > limit || field.soft > limit || field.softm > limit || field.softt > limit ||
            (field.type == FieldType::FILLER && field.array) ||
            (!field.array && ((field.count != 1 && field.type != FieldType::FILLER) || !field.counter.empty()))) util::fail("Invalid schema field");
        if (!field.counter.empty()) (void)index.at(field.counter);
        for (const auto &clause : field.enabled_by)
            for (const auto &condition : clause)
                (void)index.at(condition.field);
    }
}

void Walker::record()
{
    values.assign(schema.fields.size(), 0);
    ends.clear();
    hidden.clear();
    columns = 0;
    for (std::size_t f = 0; f < schema.fields.size(); ++f)
    {
        field(f);
        ends.push_back(columns);
    }
}

Cell Walker::cell(FieldType type, const Name &name, bool active, std::size_t size)
{
    if (++columns > limit) util::fail("Record exceeds 100000 columns");
    return visit(type, name, active && pass != Pass::Header, size);
}

std::size_t Walker::slots(std::size_t &soft, std::size_t length, const char *overflow)
{
    if (pass == Pass::Scan) return soft = std::max({soft, length, std::size_t{1}}), length;
    if (length > soft) util::fail(overflow);
    return soft;
}

bool Walker::holds(const Condition &condition) const
{
    const auto bits = static_cast<std::uint32_t>(values[index.at(condition.field)]);
    return (bits & static_cast<std::uint32_t>(condition.mask)) == static_cast<std::uint32_t>(condition.value);
}

bool Walker::enabled(const Field &field) const
{
    if (pass == Pass::Header || field.enabled_by.empty()) return true;
    for (const auto &clause : field.enabled_by)
        if (std::all_of(clause.begin(), clause.end(), [&](const Condition &c)
                        { return holds(c); })) return true;
    return false;
}

void Walker::field(std::size_t f)
{
    auto &field = schema.fields[f];
    const bool on = enabled(field);
    std::size_t length = field.type == FieldType::FILLER ? 1 : field.count, count = length;
    if (!field.counter.empty())
    {
        length = pass == Pass::Header || !on ? 0 : checked_count(values[index.at(field.counter)]);
        count = slots(field.soft, length, "Array exceeds SOFT width; disassemble and export an updated DDF first");
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        const Name name{field, i, field.array && count > 1};
        const bool active = on && i < length;
        if (field.type >= FieldType::MTX) compound(field, name, active);
        else if (const auto value = cell(field.type, name, active, field.type == FieldType::FILLER ? field.count : 0); i == 0 && value && index.primary[f])
            values[f] = value->integer;
    }
}

void Walker::compound(Field &field, const Name &element, bool active)
{
    const bool material = field.type >= FieldType::MAT;
    for (const bool mesh : {true, false})
    {
        if (material && !mesh) break;
        if (material ? !schema.mat_counts : !schema.mtx_counts) hidden.push_back(columns);
        const auto count = cell(FieldType::INT, {field, element.element, element.indexed, material ? "_cnt" : (mesh ? "_cntm" : "_cntt")}, active);
        if (field.type == FieldType::MAT2) cell(FieldType::INT, {field, element.element, element.indexed, "_extra"}, active);
        const auto length = count ? checked_count(count->integer) : 0;
        const auto n = slots(mesh ? field.softm : field.softt, length, "Compound count exceeds SOFTM/SOFTT; export an updated DDF first");
        for (std::size_t j = 0; j < n; ++j)
            for (const auto &[type, prefix, tail] : parts(field.type, mesh))
                cell(type, {field, element.element, element.indexed, prefix, j, tail}, active && j < length);
    }
    if (field.type == FieldType::MTX3) cell(FieldType::UNICODE, {field, element.element, element.indexed, "_tE"}, active);
}

Walker header(Schema &schema, const Index &index, std::vector<std::string> &names)
{
    Walker walker{schema, index, Pass::Header, [&](FieldType, const Name &name, bool, std::size_t)
                  { return names.push_back(name.str()), Cell{}; }};
    walker.record();
    return walker;
}
} // namespace layout
