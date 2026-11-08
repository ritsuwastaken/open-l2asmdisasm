#include "binary.h"
#include "util.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace binary
{
using namespace l2asmdisasm;

std::span<const std::uint8_t> Reader::take(std::size_t count)
{
    if (count > bytes.size() - offset) util::fail("Truncated DAT at byte " + std::to_string(offset));
    const auto result = bytes.subspan(offset, count);
    offset += count;
    return result;
}

std::uint32_t Reader::integer(std::size_t size)
{
    const auto data = take(size);
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < size; ++i)
        value |= static_cast<std::uint32_t>(data[i]) << (i * 8);
    return value;
}

std::int32_t Reader::compact()
{
    const auto decoded = decode_compact_index(bytes.subspan(offset));
    offset += decoded.bytes_read;
    return decoded.value;
}

void put(std::vector<std::uint8_t> &output, std::uint32_t value, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i)
        output.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}

l2asmdisasm::CompactIndex decode_compact_index(std::span<const std::uint8_t> input)
{
    if (input.empty()) throw std::invalid_argument("Truncated compact index");

    const bool negative = (input[0] & 0x80) != 0;
    std::uint64_t magnitude = input[0] & 0x3f;
    bool more = (input[0] & 0x40) != 0;
    std::size_t size = 1;
    unsigned shift = 6;
    while (more)
    {
        if (size >= input.size()) throw std::invalid_argument("Truncated compact index");
        const auto byte = input[size++];
        if (size == 5)
        {
            if (byte > 0x1f) throw std::invalid_argument("Compact index overflow");
            magnitude |= static_cast<std::uint64_t>(byte) << shift;
            more = false;
        }
        else
        {
            magnitude |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
            more = (byte & 0x80) != 0;
            shift += 7;
        }
    }

    const auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) + (negative ? 1 : 0);
    if (magnitude > limit) throw std::invalid_argument("Compact index overflow");
    const auto value = negative ? -static_cast<std::int64_t>(magnitude) : static_cast<std::int64_t>(magnitude);
    return {static_cast<std::int32_t>(value), size};
}

std::vector<std::uint8_t> encode_compact_index(std::int32_t value)
{
    const bool negative = value < 0;
    auto magnitude = static_cast<std::uint64_t>(negative ? -static_cast<std::int64_t>(value) : value);
    std::vector<std::uint8_t> output;
    output.reserve(5);
    auto first = static_cast<std::uint8_t>((magnitude & 0x3f) | (negative ? 0x80 : 0));
    magnitude >>= 6;
    if (magnitude != 0) first |= 0x40;
    output.push_back(first);
    while (magnitude != 0)
    {
        auto byte = static_cast<std::uint8_t>(magnitude & 0x7f);
        magnitude >>= 7;
        if (magnitude != 0) byte |= 0x80;
        output.push_back(byte);
    }
    return output;
}

Value read(Reader &reader, FieldType type, std::size_t size)
{
    if (type == FieldType::PADDING)
    {
        const auto bytes = reader.take(size);
        if (!std::all_of(bytes.begin(), bytes.end(), [&](auto byte)
                         { return byte == bytes.front(); }))
            util::fail("Nonuniform padding cannot be represented by one byte value");
        return {bytes.front()};
    }
    if (type == FieldType::STRING || type == FieldType::UTF16)
    {
        const auto count = type == FieldType::STRING ? reader.compact() : std::bit_cast<std::int32_t>(reader.integer(4));
        if (type == FieldType::UTF16 && count < 0) util::fail("Negative UTF-16 byte length");
        const bool wide = type == FieldType::UTF16 || count < 0;
        const auto length = count < 0 ? -static_cast<std::int64_t>(count) * 2 : static_cast<std::int64_t>(count);
        return {encoding::decode(reader.take(static_cast<std::size_t>(length)), wide), wide};
    }
    if (type == FieldType::COMPACT) return {reader.compact()};
    if (type == FieldType::I64 || type == FieldType::F64)
    {
        const std::uint64_t low = reader.integer(4);
        return {static_cast<std::int64_t>(low | static_cast<std::uint64_t>(reader.integer(4)) << 32)};
    }
    const auto &info = util::type_info(type);
    const auto bits = reader.integer(info.size);
    if (info.signed_value && (bits & (UINT32_C(1) << (info.size * 8 - 1))))
        return {std::int64_t(bits) - (INT64_C(1) << (info.size * 8))};
    return {bits};
}

void write(std::vector<std::uint8_t> &output, FieldType type, const Value &value, std::size_t size)
{
    if (type == FieldType::PADDING)
    {
        output.insert(output.end(), size, static_cast<std::uint8_t>(value.integer));
        return;
    }
    if (type == FieldType::STRING || type == FieldType::UTF16)
    {
        const bool wide = type == FieldType::UTF16 || value.wide;
        const auto bytes = encoding::encode(value.text, wide);
        if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) util::fail("String too long");
        if (type == FieldType::STRING)
        {
            if (wide && bytes.empty()) util::fail("Empty UTF-16 string has no distinct on-disk encoding; use byte encoding");
            const auto encoded = encode_compact_index(wide ? -static_cast<std::int32_t>(bytes.size() / 2) : static_cast<std::int32_t>(bytes.size()));
            output.insert(output.end(), encoded.begin(), encoded.end());
        }
        else put(output, static_cast<std::uint32_t>(bytes.size()), 4);
        output.insert(output.end(), bytes.begin(), bytes.end());
        return;
    }
    if (type == FieldType::COMPACT)
    {
        const auto encoded = encode_compact_index(static_cast<std::int32_t>(value.integer));
        output.insert(output.end(), encoded.begin(), encoded.end());
        return;
    }
    if (type == FieldType::I64 || type == FieldType::F64)
    {
        const auto bits = static_cast<std::uint64_t>(value.integer);
        put(output, static_cast<std::uint32_t>(bits), 4);
        put(output, static_cast<std::uint32_t>(bits >> 32), 4);
        return;
    }
    put(output, static_cast<std::uint32_t>(value.integer), util::min_size(type));
}
} // namespace binary
