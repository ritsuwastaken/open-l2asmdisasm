#ifndef DAT_H
#define DAT_H

#include <cstdint>
#include <l2asmdisasm.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dat
{
std::string disassemble(std::span<const std::uint8_t> input, const l2asmdisasm::Schema &definition, l2asmdisasm::Schema *sized_schema,
                        const l2asmdisasm::TextOptions &options);
std::vector<std::uint8_t> assemble(std::string_view text, const l2asmdisasm::Schema &definition, const l2asmdisasm::TextOptions &options);
} // namespace dat

#endif // DAT_H
