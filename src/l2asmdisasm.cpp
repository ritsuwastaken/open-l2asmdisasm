#include "l2asmdisasm_private.h" // IWYU pragma: keep

#include "binary.h"
#include "encoding.h"

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

} // namespace l2asmdisasm
