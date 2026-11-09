#ifndef ENCODING_H
#define ENCODING_H

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace l2asmdisasm::encoding
{
void validate_utf8(std::string_view text);
std::string decode(std::span<const std::uint8_t> bytes, bool wide);
std::vector<std::uint8_t> encode(std::string_view text, bool wide);
} // namespace l2asmdisasm::encoding

#endif // ENCODING_H
