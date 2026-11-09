#include "binary.h"
#include "encoding.h"
#include <algorithm>
#include <format>
#include <utility>

namespace l2asmdisasm::binary
{
namespace
{
constexpr std::uint8_t sign_bit = 0x80, first_more = 0x40, first_payload = 0x3f, more = 0x80, payload = 0x7f;
constexpr unsigned last_shift = 6 + 3 * 7;

Value read_padding(std::span<const std::uint8_t> bytes)
{
    if (!std::ranges::all_of(bytes, [&](auto byte)
                             { return byte == bytes.front(); }))
        throw std::invalid_argument("Nonuniform padding cannot be represented by one byte value");
    return {bytes.front()};
}

Value read_text(Reader &reader, std::uint64_t length, bool wide)
{
    return {.text = encoding::decode(reader.take(length), wide), .wide = wide};
}

Value read_number(Reader &reader, const TypeInfo &info)
{
    const auto bits = reader.integer(info.size);
    const auto shift = 64 - info.width;
    if (!info.is_signed) return {static_cast<std::int64_t>(bits)};
    return {static_cast<std::int64_t>(bits << shift) >> shift};
}

void write_string(std::vector<std::uint8_t> &output, FieldType type, const Value &value)
{
    const bool wide = type == FieldType::UTF16 || value.wide;
    const auto bytes = encoding::encode(value.text, wide);
    if (!std::in_range<std::int32_t>(bytes.size())) throw std::invalid_argument("String too long");
    const auto length = static_cast<std::int32_t>(bytes.size());
    if (type == FieldType::UTF16) put(output, length, 4);
    else if (!wide) put_compact(output, length);
    else if (length == 0) throw std::invalid_argument("Empty UTF-16 string has no distinct on-disk encoding; use byte encoding");
    else put_compact(output, -(length / 2));
    output.insert(output.end(), bytes.begin(), bytes.end());
}
} // namespace

std::span<const std::uint8_t> Reader::take(std::uint64_t count)
{
    if (count > bytes.size() - offset) throw std::invalid_argument(std::format("Truncated DAT at byte {}", offset));
    const auto result = bytes.subspan(offset, static_cast<std::size_t>(count));
    offset += static_cast<std::size_t>(count);
    return result;
}

std::uint64_t Reader::integer(std::size_t size)
{
    const auto data = take(size);
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < size; ++i)
        value |= static_cast<std::uint64_t>(data[i]) << (i * 8);
    return value;
}

std::int32_t Reader::compact()
{
    const auto next = [&]
    {
        if (offset == bytes.size()) throw std::invalid_argument("Truncated compact index");
        return bytes[offset++];
    };
    auto byte = next();
    const bool negative = byte & sign_bit;
    std::uint64_t magnitude = byte & first_payload;
    bool continued = byte & first_more;
    for (unsigned shift = 6; continued; shift += 7)
    {
        byte = next();
        magnitude |= static_cast<std::uint64_t>(shift == last_shift ? byte : byte & payload) << shift;
        continued = shift != last_shift && (byte & more);
    }
    if (magnitude > static_cast<std::uint64_t>(INT32_MAX) + negative) throw std::invalid_argument("Compact index overflow");
    return static_cast<std::int32_t>(negative ? -static_cast<std::int64_t>(magnitude) : static_cast<std::int64_t>(magnitude));
}

void put(std::vector<std::uint8_t> &output, std::uint64_t value, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i)
        output.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}

void put_compact(std::vector<std::uint8_t> &output, std::int32_t value)
{
    auto magnitude = static_cast<std::uint32_t>(value < 0 ? -static_cast<std::int64_t>(value) : value);
    const auto first = static_cast<std::uint8_t>((value < 0 ? sign_bit : 0) | (magnitude & first_payload));
    magnitude >>= 6;
    output.push_back(static_cast<std::uint8_t>(first | (magnitude ? first_more : 0)));
    for (; magnitude; magnitude >>= 7)
        output.push_back(static_cast<std::uint8_t>((magnitude & payload) | (magnitude > payload ? more : 0)));
}

Value read(Reader &reader, FieldType type, std::size_t size)
{
    switch (type)
    {
    case FieldType::PADDING:
        return read_padding(reader.take(size));
    case FieldType::STRING:
    {
        const std::int64_t count = reader.compact();
        return count < 0 ? read_text(reader, -count * 2, true) : read_text(reader, count, false);
    }
    case FieldType::UTF16:
    {
        const auto count = static_cast<std::int32_t>(reader.integer(4));
        if (count < 0) throw std::invalid_argument("Negative UTF-16 byte length");
        return read_text(reader, count, true);
    }
    case FieldType::COMPACT:
        return {reader.compact()};
    default:
        return read_number(reader, type_info(type));
    }
}

void write(std::vector<std::uint8_t> &output, FieldType type, const Value &value, std::size_t size)
{
    switch (type)
    {
    case FieldType::PADDING:
        output.insert(output.end(), size, static_cast<std::uint8_t>(value.integer));
        return;
    case FieldType::STRING:
    case FieldType::UTF16:
        return write_string(output, type, value);
    case FieldType::COMPACT:
        return put_compact(output, static_cast<std::int32_t>(value.integer));
    default:
        return put(output, static_cast<std::uint64_t>(value.integer), type_info(type).size);
    }
}
} // namespace l2asmdisasm::binary
