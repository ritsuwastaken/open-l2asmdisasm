#include "l2asmdisasm_private.h" // IWYU pragma: keep

#include "binary.h"
#include "dat.h"

namespace l2asmdisasm
{
CompactIndex decode_compact_index(std::span<const std::uint8_t> input)
{
    return binary::decode_compact_index(input);
}

std::vector<std::uint8_t> encode_compact_index(std::int32_t value)
{
    return binary::encode_compact_index(value);
}

std::vector<std::string> supported_charsets()
{
    return encoding::charset_names();
}

std::string disassemble_tsv(std::span<const std::uint8_t> input, const Schema &schema, Schema *sized_schema, const TextOptions &options)
{
    return dat::disassemble(input, schema, sized_schema, options);
}

std::vector<std::uint8_t> assemble_tsv(std::string_view text, const Schema &schema, const TextOptions &options)
{
    return dat::assemble(text, schema, options);
}
} // namespace l2asmdisasm
