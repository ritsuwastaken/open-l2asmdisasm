#ifndef JSON_H
#define JSON_H

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
constexpr std::size_t max_values = 100000;

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

    std::optional<std::size_t> inferred(const Json::Value &record, std::size_t i) const;
};

struct NameLookup
{
    NameTable *names = nullptr;
    std::unordered_map<std::string, std::uint32_t> indexes;
    std::size_t indexed = 0;

    std::uint32_t find_or_append(const std::string &text);
};

struct RecordWalker
{
    binary::Reader *reader;
    std::vector<std::uint8_t> *output;
    std::string_view source;
    std::size_t cells = 0;
    std::ptrdiff_t error_offset = -1;
    const NameTable *names = nullptr;
    NameLookup lookup;

    void row(RecordPlan &plan, Json::Value &value, std::size_t block, std::uint64_t index);

    void record(RecordPlan &plan, Json::Value &record, const std::string &path = {});
};

Schema schema_value(const Json::Value &root);
std::vector<std::unique_ptr<RecordPlan>> record_plans(const std::vector<Block> &blocks);
std::string write_document(const Json::Value &value, const std::vector<std::unique_ptr<RecordPlan>> &plans);

Json::Value make_object(std::initializer_list<std::pair<std::string, Json::Value>> members);
Json::Value array();
const Json::Value &at(const Json::Value &value, const std::string &key);
const Json::Value &at(const Json::Value &value, std::size_t index);
std::string string(const Json::Value &value);
bool boolean(const Json::Value &value);
Json::Value parse_json(std::string_view text);
std::string source_location(std::string_view source, std::ptrdiff_t offset);
void object(const Json::Value &value, std::initializer_list<std::string_view> allowed);
std::int64_t integer(const Json::Value &value);
std::size_t count(const Json::Value &value, std::size_t minimum = 0);
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

#endif // JSON_H
