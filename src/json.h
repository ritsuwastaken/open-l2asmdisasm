#ifndef L2ASMDISASM_JSON_H
#define L2ASMDISASM_JSON_H

#include "util.h"
#include <initializer_list>
#include <json/json.h>
#include <utility>

namespace l2asmdisasm::json
{
using JsonValue = Json::Value;
JsonValue make_object(std::initializer_list<std::pair<std::string, JsonValue>> members);
JsonValue array(std::initializer_list<JsonValue> values = {});
const JsonValue &at(const JsonValue &value, const std::string &key);
const JsonValue &at(const JsonValue &value, std::size_t index);
std::string string(const JsonValue &value);
bool boolean(const JsonValue &value);
JsonValue parse_json(std::string_view text);
std::string write_json(const JsonValue &value);
void object(const JsonValue &value, std::initializer_list<std::string_view> allowed);
std::int64_t integer(const JsonValue &value);
std::size_t count(const JsonValue &value, std::size_t minimum = 0);
std::vector<Block> blocks(const Schema &schema);

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
