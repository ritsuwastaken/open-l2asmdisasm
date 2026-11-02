#include "ddf.h"
#include "util.h"
#include <charconv>
#include <stdexcept>

namespace ddf
{
using namespace l2asmdisasm;

std::string write(const Schema &schema)
{
    std::string fs = "FS = \"";
    for (unsigned char c : schema.separator)
    {
        if (c == '\t') fs += "\\t";
        else if (c == '\\' || c == '"')
        {
            fs += '\\';
            fs += static_cast<char>(c);
        }
        else fs += static_cast<char>(c);
    }
    const auto count = [](const Block &block)
    {
        if (!block.counted_by.empty()) return block.counted_by;
        if (block.record_count == Block::stored) return std::string("OFF");
        if (block.record_count == Block::compact) return std::string("CNTR");
        if (block.record_count == Block::to_end) return std::string("EOF");
        return std::to_string(block.record_count);
    };
    std::string output = fs + "\";\nRECCNT = " + count(schema) +
                         ";\nHEADER = " + (schema.header ? "YES" : "NO") + ";\nMTXCNT_OUT = " + (schema.mtx_counts ? "YES" : "NO") +
                         ";\nMATCNT_OUT = " + (schema.mat_counts ? "YES" : "NO") +
                         ";\nORD_IGNORE = " + (schema.ignore_order ? "YES" : "NO") +
                         ";\nMAGIC = " + std::to_string(schema.magic) + ";\n" + (schema.trailer ? "" : "TRAILER = NO;\n");
    for (std::size_t b = 0; b <= schema.blocks.size(); ++b)
    {
        const Block &block = b ? schema.blocks[b - 1] : schema;
        if (b) output += "\nRECCNT = " + count(block) + ";\n";
        output += "\n{\n";
        for (const auto &field : block.fields)
        {
            if (static_cast<std::size_t>(field.type) >= util::type_names.size()) throw std::invalid_argument("Invalid field type");
            output += "\t" + std::string(util::type_names[static_cast<std::size_t>(field.type)]) + " " + field.name;
            if (field.type == FieldType::FILLER) output += "{" + std::to_string(field.count) + "}";
            if (field.array) output += "[" + (field.counter.empty() ? std::to_string(field.count) : field.counter) + "]";
            output += ";\n";
            if (field.order) output += "\t\tORD = " + (*field.order == -1 ? std::string("OFF") : std::to_string(*field.order)) + ";\n";
            if (!field.counter.empty()) output += "\t\tSOFT = " + std::to_string(field.soft) + ";\n";
            if (field.type >= FieldType::MTX) output += "\t\tSOFTM = " + std::to_string(field.softm) + ";\n";
            if (field.type >= FieldType::MTX && field.type <= FieldType::MTX3) output += "\t\tSOFTT = " + std::to_string(field.softt) + ";\n";
            for (const auto &clause : field.enabled_by)
            {
                output += "\t\tENBBY = [";
                for (std::size_t i = 0; i < clause.size(); ++i)
                {
                    const auto &condition = clause[i];
                    if (i != 0) output += ", ";
                    output += "(" + condition.field + ": " + std::to_string(condition.mask) + ", " + std::to_string(condition.value) + ")";
                }
                output += "];\n";
            }
            for (const auto &rule : field.skip_if)
            {
                auto numeric = [&](double value)
                {
                    if (field.type != FieldType::FLOAT) return std::to_string(static_cast<std::int64_t>(value));
                    char buffer[64];
                    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
                    if (result.ec != std::errc{}) throw std::invalid_argument("Cannot export SKIPIF value");
                    return std::string(buffer, result.ptr);
                };
                output += "\t\tSKIPIF = [(" + std::to_string(rule.first);
                if (rule.first != rule.last) output += " .. " + std::to_string(rule.last);
                output += ", " + numeric(rule.low);
                if (rule.low != rule.high) output += " .. " + numeric(rule.high);
                output += ")];\n";
            }
        }
        output += "}\n";
    }
    return output;
}
} // namespace ddf
