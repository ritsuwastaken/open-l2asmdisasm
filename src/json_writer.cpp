#include "json_writer.h"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <iterator>

namespace l2asmdisasm::json
{
Writer::Writer(std::ostream &stream, std::size_t blocks) : stream(stream), depth(blocks == 1 ? 0 : 2)
{
    if (depth) output += "{\n  \"blocks\": [";
}

void Writer::begin_block()
{
    if (depth)
    {
        output += blocks++ ? ",\n" : "\n";
        indent(depth);
    }
    output += '[';
    records = 0;
}

void Writer::record(const Json::Value &record, const RecordPlan &plan)
{
    output += records++ ? ",\n" : "\n";
    indent(depth + 1);
    write(record, &plan, nullptr, depth + 1);
    flush();
}

void Writer::end_block()
{
    if (records)
    {
        output += '\n';
        indent(depth);
    }
    output += ']';
}

void Writer::finish()
{
    if (depth) output += "\n  ]\n}";
    output += '\n';
    flush();
}

void Writer::flush()
{
    stream.write(output.data(), static_cast<std::streamsize>(output.size()));
    output.clear();
}

void Writer::string(std::string_view text)
{
    constexpr std::string_view special = "\"\\\b\f\n\r\t", escaped = "\"\\bfnrt";
    output += '"';
    for (auto i = text.begin();; ++i)
    {
        const auto plain = std::find_if(i, text.end(), [](unsigned char c)
                                        { return c < 0x20 || c == '"' || c == '\\'; });
        output.append(i, plain);
        if ((i = plain) == text.end()) break;
        if (const auto k = special.find(*i); k != std::string_view::npos) (output += '\\') += escaped[k];
        else std::format_to(std::back_inserter(output), "\\u{:04x}", *i);
    }
    output += '"';
}

void Writer::scalar(const Json::Value &value)
{
    if (const char *begin, *end; value.getString(&begin, &end)) string({begin, end});
    else if (value.isBool()) output += value.asBool() ? "true" : "false";
    else if (value.isNull()) output += "null";
    else output += std::to_string(value.asInt64());
}

void Writer::write_float(double value, FieldType type)
{
    if (value == 0 && std::signbit(value))
    {
        output += "-0.0";
        return;
    }
    char buffer[128];
    auto result = type == FieldType::F32 ? std::to_chars(buffer, buffer + sizeof(buffer), static_cast<float>(value))
                                         : std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{}) throw std::invalid_argument("Cannot format JSON float");
    if (type == FieldType::F32)
    {
        double parsed = 0;
        const auto check = std::from_chars(buffer, result.ptr, parsed);
        if (check.ec != std::errc{} || std::bit_cast<std::uint32_t>(static_cast<float>(parsed)) != std::bit_cast<std::uint32_t>(static_cast<float>(value)))
            result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    }
    output.append(buffer, result.ptr);
}

void Writer::write(const Json::Value &value, const RecordPlan *plan, const Field *field, std::size_t level, bool expand)
{
    if (!value.isObject() && !value.isArray())
    {
        if (value.type() == Json::realValue) write_float(value.asDouble(), field ? field->type : FieldType::F64);
        else scalar(value);
        return;
    }
    const bool object = value.isObject();
    const bool one_line = !expand && field && (object ? field->type != FieldType::GROUP : !plan && value.size() <= 8);
    const auto start = output.size();
    output += object ? '{' : '[';
    bool first = true;
    const auto member = [&](const Json::Value &child, const std::string *key, const RecordPlan *child_plan, const Field *child_field)
    {
        if (!first) output += ',';
        if (!one_line)
        {
            output += '\n';
            indent(level + 1);
        }
        else if (object || !first) output += ' ';
        first = false;
        if (key)
        {
            string(*key);
            output += ": ";
        }
        write(child, child_plan, child_field, level + 1);
    };
    if (object && plan)
    {
        for (std::size_t i = 0; i < plan->fields.size(); ++i)
        {
            const auto &key = plan->keys[i];
            const auto *child = value.find(key);
            if (!child) continue;
            if (const auto length = plan->inferred_count(value, i); length && integer(*child) == static_cast<std::int64_t>(*length)) continue;
            member(*child, &key, plan->groups[i].get(), &plan->fields[i]);
        }
    }
    else if (object)
        for (const auto &key : value.getMemberNames())
            member(value[key], &key, nullptr, nullptr);
    else
        for (const auto &child : value)
            member(child, nullptr, plan, field);
    if (!first)
    {
        if (!one_line)
        {
            output += '\n';
            indent(level);
        }
        else if (object) output += ' ';
    }
    output += object ? '}' : ']';
    if (one_line && !object && output.size() - start > 100)
    {
        output.resize(start);
        write(value, plan, field, level, true);
    }
}
} // namespace l2asmdisasm::json
