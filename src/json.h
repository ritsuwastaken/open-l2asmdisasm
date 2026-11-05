#ifndef L2ASMDISASM_JSON_H
#define L2ASMDISASM_JSON_H

#include "binary.h"
#include "util.h"
#include <initializer_list>
#include <json/json.h>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

namespace l2asmdisasm::json
{
Schema parse_schema(std::string_view input, std::string_view client_version);
std::vector<Schema> parse_schemas(std::string_view input);
std::string export_schema(const Schema &schema);
std::string export_schemas(std::span<const Schema> schemas);
std::filesystem::path resolve_definition(const std::filesystem::path &directory, std::string_view filename);

constexpr std::size_t limit = 100000;
using JsonValue = Json::Value;

struct RecordPlan
{
    const std::vector<Field> &fields;
    RecordPlan *parent;
    std::vector<std::string> keys;
    std::unordered_map<std::string, std::size_t> first;
    std::vector<std::unique_ptr<RecordPlan>> groups;
    std::vector<std::vector<std::size_t>> arrays;
    std::vector<bool> explicit_value;
    std::vector<std::int64_t> values;

    RecordPlan(const std::vector<Field> &fields, RecordPlan *parent = nullptr);

    std::pair<RecordPlan *, std::size_t> reference(const std::string &name);

    std::int64_t get(const std::string &name);

    std::optional<std::size_t> inferred(const JsonValue &record, std::size_t i) const;
};

struct Records
{
    binary::Reader *reader;
    std::vector<std::uint8_t> *output;
    const encoding::Charset &conversion;
    std::string_view source;
    std::size_t cells = 0;
    std::ptrdiff_t error_offset = -1;

    void row(RecordPlan &plan, JsonValue &value, std::size_t block, std::uint64_t index);

    void record(RecordPlan &plan, JsonValue &record, const std::string &path = {});
};

std::vector<std::unique_ptr<RecordPlan>> record_plans(const std::vector<Block> &parts);
std::string write_document(const JsonValue &value, const std::vector<std::unique_ptr<RecordPlan>> &plans);

JsonValue make_object(std::initializer_list<std::pair<std::string, JsonValue>> members);
JsonValue array();
const JsonValue &at(const JsonValue &value, const std::string &key);
const JsonValue &at(const JsonValue &value, std::size_t index);
std::string string(const JsonValue &value);
bool boolean(const JsonValue &value);
JsonValue parse_json(std::string_view text);
std::string source_location(std::string_view source, std::ptrdiff_t offset);
std::string write_json(const JsonValue &value);
void object(const JsonValue &value, std::initializer_list<std::string_view> allowed);
std::int64_t integer(const JsonValue &value);
std::size_t count(const JsonValue &value, std::size_t minimum = 0);
void validate_schema(const Schema &schema);

template <typename Function>
auto checked_json(Function function)
{
    try
    {
        return function();
    }
    catch (const Json::Exception &error)
    {
        util::fail(std::string("JSON: ") + error.what());
    }
}

} // namespace l2asmdisasm::json

#endif
