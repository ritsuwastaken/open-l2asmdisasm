#include "json.h"
#include <algorithm>
#include <set>
#include <utility>

namespace l2asmdisasm::json
{
namespace
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

bool enabled(RecordPlan &plan, const Field &field)
{
    return field.enabled_by.empty() ||
           std::any_of(field.enabled_by.begin(), field.enabled_by.end(), [&](const auto &clause)
                       { return std::all_of(clause.begin(), clause.end(), [&](const Condition &c)
                                            { return (std::uint32_t(plan.get(c.field)) & std::uint32_t(c.mask)) == std::uint32_t(c.value); }); });
}

template <typename Function>
decltype(auto) at_path(const std::string &segment, const Json::Value &node, Function &&function)
{
    try
    {
        return located(node, function);
    }
    catch (LocatedError &error)
    {
        error.path = error.path.empty() ? segment : segment + (error.path.front() == '[' ? "" : ".") + error.path;
        throw;
    }
}
} // namespace

RecordPlan::RecordPlan(const std::vector<Field> &fields, RecordPlan *parent)
    : fields(fields), parent(parent), keys(field_keys(fields)), groups(fields.size()), counted(fields.size()), not_inferable(fields.size())
{
    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        const auto &field = fields[i];
        if (!field.counter.empty())
        {
            const auto [scope, counter] = resolve(field.counter);
            if (scope == this) counted[counter].push_back(i);
            else scope->not_inferable[counter] = true;
        }
        for (const auto &clause : field.enabled_by)
            for (const auto &condition : clause)
            {
                const auto [scope, referenced] = resolve(condition.field);
                scope->not_inferable[referenced] = true;
            }
        if (field.type == FieldType::GROUP) groups[i] = std::make_unique<RecordPlan>(field.members, this);
        const auto [found, unique] = index.emplace(field.name, i);
        if (!unique) not_inferable[found->second] = not_inferable[i] = true;
    }
}

std::pair<RecordPlan *, std::size_t> RecordPlan::resolve(const std::string &name)
{
    if (const auto found = index.find(name); found != index.end()) return {this, found->second};
    if (parent) return parent->resolve(name);
    throw std::invalid_argument("Missing preceding integer field: " + name);
}

std::int64_t RecordPlan::get(const std::string &name)
{
    const auto [scope, i] = resolve(name);
    return scope->values[i];
}

std::optional<std::size_t> RecordPlan::inferred_count(const Json::Value &record, std::size_t i) const
{
    const auto &field = fields[i];
    if (not_inferable[i] || counted[i].empty() || field.array || !binary::is_integer(field.type) || !field.enabled_by.empty()) return std::nullopt;
    std::optional<std::size_t> length;
    for (const auto array : counted[i])
    {
        if (!record.isMember(keys[array])) continue;
        const auto &value = record[keys[array]];
        if (!value.isArray()) throw std::invalid_argument("Expected array for " + keys[array]);
        if (length && *length != value.size()) throw std::invalid_argument("Arrays sharing counter '" + field.name + "' have different lengths");
        length = value.size();
    }
    return length;
}

std::vector<std::unique_ptr<RecordPlan>> record_plans(const std::vector<Block> &blocks)
{
    std::vector<std::unique_ptr<RecordPlan>> plans;
    for (const auto &block : blocks)
    {
        auto plan = std::make_unique<RecordPlan>(block.fields);
        if (!block.nonzero.empty()) plan->not_inferable[plan->index.at(block.nonzero)] = true;
        plans.push_back(std::move(plan));
    }
    return plans;
}

void RecordWalker::row(RecordPlan &plan, Json::Value &record, std::size_t block, std::uint64_t index)
{
    const auto start = position();
    const auto fail = [&](const std::string &message, std::ptrdiff_t offset)
    {
        throw std::invalid_argument("Block " + std::to_string(block + 1) + ", record " + std::to_string(index + 1) +
                                    (reading() ? ", byte " + std::to_string(start) : "") + ": " + message + source_location(source, offset));
    };
    cells = 0;
    try
    {
        walk(plan, record);
        if (position() == start) throw std::invalid_argument("Record consumes no bytes");
    }
    catch (const LocatedError &error)
    {
        fail(error.path + ": " + error.what(), error.offset);
    }
    catch (const std::invalid_argument &error)
    {
        fail(error.what(), record.getOffsetStart());
    }
}

void RecordWalker::walk(RecordPlan &plan, Json::Value &record)
{
    if (reading()) record = Json::Value(Json::objectValue);
    if (!record.isObject()) throw std::invalid_argument("Expected record object");
    plan.values.assign(plan.fields.size(), 0);
    std::size_t present = 0;
    for (std::size_t f = 0; f < plan.fields.size(); ++f)
    {
        const auto &key = plan.keys[f];
        const auto &node = record.isMember(key) ? std::as_const(record)[key] : record;
        if (enabled(plan, plan.fields[f]))
        {
            ++present;
            at_path(key, node, [&]
                    { walk_field(plan, f, record); });
        }
        else if (record.isMember(key)) throw LocatedError("Disabled field must be omitted", node.getOffsetStart(), key);
    }
    if (record.size() == present) return;
    for (const auto &key : record.getMemberNames())
        if (std::find(plan.keys.begin(), plan.keys.end(), key) == plan.keys.end())
            throw LocatedError("Unknown record field", record[key].getOffsetStart(), key);
}

void RecordWalker::walk_field(RecordPlan &plan, std::size_t f, Json::Value &record)
{
    const auto &field = plan.fields[f];
    const auto n = length(plan, f, record);
    auto &value = record[plan.keys[f]];
    std::int64_t first = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        std::int64_t cell;
        if (!field.array) cell = walk_item(plan, f, value);
        else
        {
            auto &item = value[static_cast<Json::ArrayIndex>(i)];
            cell = at_path("[" + std::to_string(i) + "]", item, [&]
                           { return walk_item(plan, f, item); });
        }
        if (i == 0) first = cell;
    }
    if (plan.index.at(field.name) == f) plan.values[f] = first;
}

std::int64_t RecordWalker::walk_item(RecordPlan &plan, std::size_t f, Json::Value &item)
{
    const auto &field = plan.fields[f];
    if (field.type == FieldType::GROUP)
    {
        walk(*plan.groups[f], item);
        return 0;
    }
    if (reading())
    {
        const auto cell = binary::read(*reader, field.type, field.size);
        item = encode_field(cell, field, names);
        return cell.integer;
    }
    const auto cell = decode_field(item, field, lookup);
    binary::write(*output, field.type, cell, field.size);
    return cell.integer;
}

std::size_t RecordWalker::length(RecordPlan &plan, std::size_t f, Json::Value &record)
{
    const auto &field = plan.fields[f];
    const auto &key = plan.keys[f];
    const auto n = field.counter.empty() ? field.count : count(Json::Int64(plan.get(field.counter)));
    cells += std::max(n, std::size_t{1});
    if (cells > max_values) throw std::invalid_argument("Record exceeds 100000 values");
    if (!reading() && !record.isMember(key))
    {
        if (const auto inferred = plan.inferred_count(record, f)) record[key] = Json::UInt64(*inferred);
        else (void)at(record, key);
    }
    auto &value = record[key];
    if (field.array && reading()) value.resize(static_cast<Json::ArrayIndex>(n));
    if (field.array && (!value.isArray() || value.size() != n)) throw std::invalid_argument("Array length differs from count");
    return n;
}
} // namespace l2asmdisasm::json
