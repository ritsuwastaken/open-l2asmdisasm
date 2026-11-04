#include "json.h"
#include "binary.h"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>

namespace l2asmdisasm::json
{
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

JsonValue encode_value(const binary::Value &v, FieldType type)
{
    if (type == FieldType::UTF16) return v.text;
    if (type == FieldType::STRING) return make_object({{"encoding", v.wide ? "utf16" : "byte"}, {"value", v.text}});
    if (type == FieldType::I64) return std::to_string(v.integer);
    if (type == FieldType::F32 || type == FieldType::F64)
    {
        const double number = type == FieldType::F32 ? double(std::bit_cast<float>(std::uint32_t(v.integer))) : std::bit_cast<double>(v.integer);
        if (!std::isfinite(number)) return make_object({{"bits", util::hex(static_cast<std::uint64_t>(v.integer) & (type == FieldType::F32 ? UINT64_C(0xffffffff) : UINT64_MAX))}});
        return number;
    }
    return JsonValue(Json::Int64(v.integer));
}

binary::Value decode_value(const JsonValue &v, FieldType type)
{
    if (type == FieldType::UTF16) return {string(v), true};
    if (type == FieldType::STRING)
    {
        object(v, {"encoding", "value"});
        const auto encoding = string(at(v, "encoding"));
        if (encoding != "byte" && encoding != "utf16") util::fail("string encoding must be byte or utf16");
        return {string(at(v, "value")), encoding == "utf16"};
    }
    if (type == FieldType::F32 || type == FieldType::F64)
    {
        if (v.isObject())
        {
            object(v, {"bits"});
            const auto bits = util::number<std::uint64_t>(string(at(v, "bits")), 16);
            if (type == FieldType::F32 && bits > UINT32_MAX) util::fail("f32 bits overflow");
            const double number = type == FieldType::F32 ? double(std::bit_cast<float>(std::uint32_t(bits))) : std::bit_cast<double>(bits);
            if (std::isfinite(number)) util::fail("bits is reserved for non-finite floats");
            return {std::bit_cast<std::int64_t>(bits)};
        }
        if (!v.isNumeric()) util::fail("Expected JSON number");
        const auto number = v.asDouble();
        if (!std::isfinite(number) || (type == FieldType::F32 && !std::isfinite(static_cast<float>(number)))) util::fail("Float overflow");
        if (type == FieldType::F32) return {std::bit_cast<std::uint32_t>(static_cast<float>(number))};
        return {std::bit_cast<std::int64_t>(number)};
    }
    if (type == FieldType::I64)
    {
        if (!v.isString()) util::fail("i64 requires a decimal string");
        return {util::number<std::int64_t>(string(v))};
    }
    const auto n = integer(v);
    const bool signed_value = util::type_info(type).signed_value;
    const auto bits = type == FieldType::COMPACT ? 32 : util::min_size(type) * 8;
    const auto minimum = signed_value ? -(INT64_C(1) << (bits - 1)) : 0;
    const auto maximum = (INT64_C(1) << (bits - (signed_value ? 1 : 0))) - 1;
    if (n < minimum || n > maximum) util::fail("Integer field overflow");
    return {n};
}

RecordPlan::RecordPlan(const std::vector<Field> &fields, RecordPlan *parent)
    : fields(fields), parent(parent), keys(field_keys(fields)), groups(fields.size()), arrays(fields.size()), explicit_value(fields.size())
{
    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        const auto [found, unique] = first.emplace(fields[i].name, i);
        if (!unique) explicit_value[found->second] = explicit_value[i] = true;
    }
    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        const auto &field = fields[i];
        if (!field.counter.empty())
        {
            const auto [scope, counter] = reference(field.counter);
            if (scope == this) arrays[counter].push_back(i);
            else scope->explicit_value[counter] = true;
        }
        for (const auto &clause : field.enabled_by)
            for (const auto &condition : clause)
            {
                const auto [scope, counter] = reference(condition.field);
                scope->explicit_value[counter] = true;
            }
        if (field.type == FieldType::GROUP) groups[i] = std::make_unique<RecordPlan>(field.members, this);
    }
}

std::pair<RecordPlan *, std::size_t> RecordPlan::reference(const std::string &name)
{
    if (const auto found = first.find(name); found != first.end()) return {this, found->second};
    if (parent) return parent->reference(name);
    util::fail("Missing preceding integer field: " + name);
}

std::int64_t RecordPlan::get(const std::string &name)
{
    const auto [scope, index] = reference(name);
    return scope->values[index];
}

std::optional<std::size_t> RecordPlan::inferred(const JsonValue &record, std::size_t i) const
{
    const auto &field = fields[i];
    if (explicit_value[i] || arrays[i].empty() || field.array || !util::is_integer(field.type) || !field.enabled_by.empty()) return std::nullopt;
    std::optional<std::size_t> length;
    for (const auto index : arrays[i])
    {
        if (!record.isMember(keys[index])) continue;
        const auto &value = record[keys[index]];
        if (!value.isArray()) util::fail("Expected array for " + keys[index]);
        if (length && *length != value.size()) util::fail("Arrays sharing counter '" + field.name + "' have different lengths");
        length = value.size();
    }
    return length;
}

std::vector<std::unique_ptr<RecordPlan>> record_plans(std::vector<Block> &parts)
{
    std::vector<std::unique_ptr<RecordPlan>> plans;
    for (auto &part : parts)
    {
        auto plan = std::make_unique<RecordPlan>(part.fields);
        if (!part.counted_by.empty()) plan->explicit_value[plan->first.at(part.counted_by)] = true;
        plans.push_back(std::move(plan));
    }
    return plans;
}

struct JsonWriter
{
    std::string output;
    Json::StreamWriterBuilder compact;

    JsonWriter()
    {
        compact["indentation"] = "";
        compact["emitUTF8"] = true;
    }

    void indent(std::size_t depth) { output.append(depth * 2, ' '); }
    void quoted(const std::string &text) { output += Json::writeString(compact, JsonValue(text)); }

    void number(double value, FieldType type)
    {
        if (value == 0 && std::signbit(value))
        {
            output += "-0.0";
            return;
        }
        char buffer[128];
        auto result = type == FieldType::F32 ? std::to_chars(buffer, buffer + sizeof(buffer), static_cast<float>(value))
                                             : std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (result.ec != std::errc{}) util::fail("Cannot format JSON float");
        if (type == FieldType::F32)
        {
            double parsed = 0;
            const auto check = std::from_chars(buffer, result.ptr, parsed);
            if (check.ec != std::errc{} || std::bit_cast<std::uint32_t>(static_cast<float>(parsed)) != std::bit_cast<std::uint32_t>(static_cast<float>(value)))
                result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        }
        output.append(buffer, result.ptr);
    }

    void write(const JsonValue &value, const RecordPlan *plan = nullptr, const Field *field = nullptr, std::size_t depth = 0, bool expand = false)
    {
        if (!value.isObject() && !value.isArray())
        {
            if (value.type() == Json::realValue) number(value.asDouble(), field ? field->type : FieldType::F64);
            else output += Json::writeString(compact, value);
            return;
        }
        const bool object = value.isObject();
        const bool in_line = !expand && field && (object ? field->type != FieldType::GROUP : !plan && value.size() <= 8);
        const auto start = output.size();
        output += object ? '{' : '[';
        bool first = true;
        const auto emit = [&](const JsonValue &child, const std::string *key, const RecordPlan *child_plan, const Field *child_field)
        {
            if (!first) output += ',';
            if (in_line)
            {
                if (object || !first) output += ' ';
            }
            else
            {
                output += '\n';
                indent(depth + 1);
            }
            first = false;
            if (key)
            {
                quoted(*key);
                output += ": ";
            }
            write(child, child_plan, child_field, depth + 1);
        };
        if (object && plan)
        {
            for (std::size_t i = 0; i < plan->fields.size(); ++i)
            {
                const auto &key = plan->keys[i];
                if (!value.isMember(key)) continue;
                if (const auto length = plan->inferred(value, i); length && integer(value[key]) == static_cast<std::int64_t>(*length)) continue;
                emit(value[key], &key, plan->groups[i].get(), &plan->fields[i]);
            }
        }
        else if (object)
        {
            for (const auto &key : value.getMemberNames())
                emit(value[key], &key, nullptr, nullptr);
        }
        else
        {
            for (const auto &child : value)
                emit(child, nullptr, plan, field);
        }
        if (!first)
        {
            if (in_line)
            {
                if (object) output += ' ';
            }
            else
            {
                output += '\n';
                indent(depth);
            }
        }
        output += object ? '}' : ']';
        if (in_line && !object && output.size() - start > 100)
        {
            output.resize(start);
            write(value, plan, field, depth, true);
        }
    }

    std::string document(const JsonValue &blocks, const std::vector<std::unique_ptr<RecordPlan>> &plans)
    {
        if (plans.size() == 1) write(at(blocks, 0), plans[0].get());
        else
        {
            output += "{\n  \"blocks\": [";
            for (std::size_t i = 0; i < plans.size(); ++i)
            {
                output += i ? ",\n" : "\n";
                indent(2);
                write(at(blocks, i), plans[i].get(), nullptr, 2);
            }
            output += "\n  ]\n}";
        }
        output += '\n';
        return std::move(output);
    }
};

std::string write_json(const JsonValue &value)
{
    JsonWriter writer;
    writer.write(value);
    writer.output += '\n';
    return std::move(writer.output);
}

void Records::row(RecordPlan &plan, JsonValue &value, std::size_t block, std::uint64_t index)
{
    const auto offset = reader ? reader->offset : output->size();
    cells = 0;
    try
    {
        record(plan, value);
        if ((reader ? reader->offset : output->size()) == offset) util::fail("Record consumes no bytes");
    }
    catch (const std::invalid_argument &error)
    {
        util::fail("Block " + std::to_string(block + 1) + ", record " + std::to_string(index + 1) +
                   (reader ? ", byte " + std::to_string(offset) : "") + ": " + error.what());
    }
}

void Records::record(RecordPlan &plan, JsonValue &record, const std::string &path)
{
    if (reader) record = JsonValue(Json::objectValue);
    if (!record.isObject()) util::fail(path + ": Expected record object");
    plan.values.assign(plan.fields.size(), 0);
    std::size_t present = 0;
    for (std::size_t f = 0; f < plan.fields.size(); ++f)
    {
        const auto &field = plan.fields[f];
        const auto &key = plan.keys[f];
        const auto where = path + key;
        try
        {
            bool enabled = field.enabled_by.empty();
            for (const auto &clause : field.enabled_by)
                enabled |= std::all_of(clause.begin(), clause.end(), [&](const Condition &c)
                                       { return (std::uint32_t(plan.get(c.field)) & std::uint32_t(c.mask)) == std::uint32_t(c.value); });
            if (!enabled)
            {
                if (record.isMember(key)) util::fail("Disabled field must be omitted");
                continue;
            }
            ++present;
            const auto n = field.counter.empty() ? (field.type == FieldType::PADDING ? 1 : field.count) : count(JsonValue(Json::Int64(plan.get(field.counter))));
            if (n > limit || cells > limit - std::max(n, std::size_t{1})) util::fail("Record exceeds 100000 values");
            cells += std::max(n, std::size_t{1});
            if (!reader && !record.isMember(key))
            {
                if (const auto length = plan.inferred(record, f)) record[key] = Json::UInt64(*length);
                else (void)at(record, key);
            }
            auto &value = record[key];
            if (field.array)
            {
                if (reader) value.resize(static_cast<Json::ArrayIndex>(n));
                if (!value.isArray() || value.size() != n) util::fail("Array length differs from count");
            }
            std::int64_t first = 0;
            for (std::size_t i = 0; i < n; ++i)
            {
                auto &element = field.array ? value[static_cast<Json::ArrayIndex>(i)] : value;
                if (field.type == FieldType::GROUP)
                    this->record(*plan.groups[f], element, where + (field.array ? "[" + std::to_string(i) + "]" : "") + ".");
                else
                {
                    const auto size = field.type == FieldType::PADDING ? field.count : 0;
                    const auto cell = reader ? binary::read(*reader, field.type, size, conversion) : decode_value(element, field.type);
                    if (reader) element = encode_value(cell, field.type);
                    else binary::write(*output, field.type, cell, size, conversion);
                    if (i == 0) first = cell.integer;
                }
            }
            if (plan.first.at(field.name) == f) plan.values[f] = first;
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
    if (record.size() != present)
        for (const auto &key : record.getMemberNames())
            if (std::find(plan.keys.begin(), plan.keys.end(), key) == plan.keys.end()) util::fail(path + key + ": Unknown record field");
}

std::string write_document(const JsonValue &value, const std::vector<std::unique_ptr<RecordPlan>> &plans)
{
    return JsonWriter{}.document(value, plans);
}
} // namespace l2asmdisasm::json
