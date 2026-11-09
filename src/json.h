#ifndef JSON_H
#define JSON_H

#include "binary.h"
#include <charconv>
#include <format>
#include <initializer_list>
#include <json/json.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace l2asmdisasm::json
{
constexpr std::size_t max_values = 100000;

struct LocatedError : std::invalid_argument
{
    std::ptrdiff_t offset;
    std::string path;

    LocatedError(const std::string &message, std::ptrdiff_t offset, std::string path = {})
        : std::invalid_argument(message), offset(offset), path(std::move(path)) {}
};

template <typename Function>
decltype(auto) located(const Json::Value &node, Function &&function)
{
    try
    {
        if constexpr (std::is_invocable_v<Function, const Json::Value &>) return function(node);
        else return function();
    }
    catch (const LocatedError &)
    {
        throw;
    }
    catch (const std::invalid_argument &error)
    {
        throw LocatedError(error.what(), node.getOffsetStart());
    }
    catch (const Json::Exception &error)
    {
        throw LocatedError(error.what(), node.getOffsetStart());
    }
}

template <typename T>
T number(std::string_view text, int base = 10)
{
    T value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) throw std::invalid_argument(std::format("Invalid or out-of-range integer '{}'", text));
    return value;
}

Json::Value parse_json(std::string_view text);
Json::Value make_object(std::initializer_list<std::pair<std::string, Json::Value>> members);
Json::Value array();
const Json::Value &at(const Json::Value &value, const std::string &key);
const Json::Value &at(const Json::Value &value, std::size_t index);
void object(const Json::Value &value, std::initializer_list<std::string_view> allowed);
std::string string(const Json::Value &value);
bool boolean(const Json::Value &value);
std::int64_t integer(const Json::Value &value);
std::size_t count(const Json::Value &value, std::size_t minimum = 0);
std::string source_location(std::string_view source, std::ptrdiff_t offset);

Schema schema_value(const Json::Value &root);
void validate_schema(const Schema &schema);

struct NameLookup
{
    NameTable *names = nullptr;
    std::unordered_map<std::string, std::uint32_t> indexes;
    std::size_t indexed = 0;

    std::uint32_t find_or_append(const std::string &text);
};

Json::Value encode_field(const binary::Value &cell, const Field &field, const NameTable *names);
binary::Value decode_field(const Json::Value &value, const Field &field, NameLookup &lookup);

struct RecordPlan
{
    const std::vector<Field> &fields;
    RecordPlan *parent;
    std::vector<std::string> keys;
    std::unordered_map<std::string, std::size_t> index;
    std::vector<std::unique_ptr<RecordPlan>> groups;
    std::vector<std::vector<std::size_t>> counted;
    std::vector<bool> not_inferable;
    std::vector<std::int64_t> values;

    RecordPlan(const std::vector<Field> &fields, RecordPlan *parent = nullptr);

    std::pair<RecordPlan *, std::size_t> resolve(const std::string &name);
    std::int64_t get(const std::string &name);
    std::optional<std::size_t> inferred_count(const Json::Value &record, std::size_t i) const;
};

std::vector<std::unique_ptr<RecordPlan>> record_plans(const std::vector<Block> &blocks);

struct RecordWalker
{
    binary::Reader *reader = nullptr;
    std::vector<std::uint8_t> *output = nullptr;
    std::string_view source;
    const NameTable *names = nullptr;
    NameLookup lookup;
    std::size_t cells = 0;

    void row(RecordPlan &plan, Json::Value &record, std::size_t block, std::uint64_t index);
    void walk(RecordPlan &plan, Json::Value &record);
    void walk_field(RecordPlan &plan, std::size_t f, Json::Value &record);
    std::int64_t walk_item(RecordPlan &plan, std::size_t f, Json::Value &item);
    std::size_t length(RecordPlan &plan, std::size_t f, Json::Value &record);
    bool reading() const { return reader != nullptr; }
    std::size_t position() const { return reading() ? reader->offset : output->size(); }
};

std::string write_document(const Json::Value &blocks, const std::vector<std::unique_ptr<RecordPlan>> &plans);
} // namespace l2asmdisasm::json

#endif // JSON_H
