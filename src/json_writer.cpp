#include "json.h"
#include <bit>
#include <charconv>
#include <cmath>
#include <sstream>

namespace l2asmdisasm::json
{
namespace
{
struct JsonWriter
{
    std::string output;
    std::unique_ptr<Json::StreamWriter> writer;
    std::ostringstream stream;

    JsonWriter()
    {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        builder["emitUTF8"] = true;
        writer.reset(builder.newStreamWriter());
    }

    std::string document(const Json::Value &blocks, const std::vector<std::unique_ptr<RecordPlan>> &plans)
    {
        if (plans.size() == 1) write(at(blocks, 0), plans[0].get(), nullptr, 0);
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

    void indent(std::size_t depth) { output.append(depth * 2, ' '); }

    void scalar(const Json::Value &value)
    {
        stream.str({});
        writer->write(value, &stream);
        output += stream.str();
    }

    void write_float(double value, FieldType type)
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

    void write(const Json::Value &value, const RecordPlan *plan, const Field *field, std::size_t depth, bool expand = false)
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
                indent(depth + 1);
            }
            else if (object || !first) output += ' ';
            first = false;
            if (key)
            {
                scalar(*key);
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
                if (const auto length = plan->inferred_count(value, i); length && integer(value[key]) == static_cast<std::int64_t>(*length)) continue;
                member(value[key], &key, plan->groups[i].get(), &plan->fields[i]);
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
                indent(depth);
            }
            else if (object) output += ' ';
        }
        output += object ? '}' : ']';
        if (one_line && !object && output.size() - start > 100)
        {
            output.resize(start);
            write(value, plan, field, depth, true);
        }
    }
};
} // namespace

std::string write_document(const Json::Value &blocks, const std::vector<std::unique_ptr<RecordPlan>> &plans)
{
    return JsonWriter{}.document(blocks, plans);
}
} // namespace l2asmdisasm::json
