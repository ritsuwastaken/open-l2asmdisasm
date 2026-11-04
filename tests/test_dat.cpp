#include "support.h"
#include <algorithm>

using namespace l2asmdisasm;

namespace
{
const auto schema = parse_schema(R"({"blocks":[{"fields":[
    {"name":"id","type":"u32"},{"name":"name","type":"utf16"},
    {"name":"description","type":"string"},{"name":"flags","type":"u8","count":2}
]}]})");
const auto data = R"([{"id":17,"name":"AΩ","description":{"encoding":"byte","value":"x\u0000\t\\"},"flags":[255,0]}])";
const std::vector<std::uint8_t> bytes = {
    1, 0, 0, 0, 17, 0, 0, 0, 4, 0, 0, 0, 0x41, 0, 0xa9, 3,
    4, 0x78, 0, 9, 0x5c, 0xff, 0,
    12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
}

TEST(Dat, KnownVector)
{
    EXPECT_EQ(assemble(data, schema), bytes);
    EXPECT_EQ(assemble(disassemble(bytes, schema), schema), bytes);
}

TEST(Dat, RejectsMalformedVector)
{
    for (std::size_t size = 0; size < bytes.size(); ++size)
        EXPECT_THROW((void)disassemble(std::span(bytes).first(size), schema), std::invalid_argument);
    auto malformed = bytes;
    malformed.push_back(0);
    EXPECT_THROW((void)disassemble(malformed, schema), std::invalid_argument);
    malformed = bytes;
    malformed[0] = 0xff;
    EXPECT_THROW((void)disassemble(malformed, schema), std::invalid_argument);
    malformed = bytes;
    malformed[8] = 0xff;
    EXPECT_REJECTS(disassemble(malformed, schema), "Truncated");
    malformed = bytes;
    malformed[12] = 0;
    malformed[13] = 0xd8;
    EXPECT_REJECTS(disassemble(malformed, schema), "surrogate");
}

TEST(Dat, Strings)
{
    const auto strings = parse_schema(R"({"trailer":false,"blocks":[{"fields":[{"name":"value","type":"string"}]}]})");
    const auto wide = assemble(R"([{"value":{"encoding":"utf16","value":"A😀"}}])", strings);
    EXPECT_EQ(wide, (std::vector<std::uint8_t>{1, 0, 0, 0, 0x83, 0x41, 0, 0x3d, 0xd8, 0, 0xde}));
    EXPECT_EQ(assemble(disassemble(wide, strings), strings), wide);
    EXPECT_ROUNDTRIP(strings, R"([{"value":{"encoding":"byte","value":""}},{"value":{"encoding":"byte","value":"\u0000"}},{"value":{"encoding":"utf16","value":"汉字😀\u0000"}}])");
    EXPECT_REJECTS(assemble(R"([{"value":{"encoding":"byte","value":"汉"}}])", strings), "ISO-8859-1");
    EXPECT_REJECTS(assemble(R"([{"value":{"encoding":"utf16","value":""}}])", strings), "Empty UTF-16 string");
}

TEST(Dat, IntegerRanges)
{
    const auto numbers = parse_schema(R"({"trailer":false,"blocks":[{"fields":[
        {"name":"a","type":"u32"},{"name":"b","type":"i32"},
        {"name":"c","type":"u16"},{"name":"d","type":"i16"},
        {"name":"e","type":"u8"},{"name":"f","type":"i8"}
    ]}]})");
    const auto packed = assemble(R"([{"a":4294967295,"b":-2147483648,"c":65535,"d":-32768,"e":255,"f":-128}])", numbers);
    EXPECT_EQ(packed, (std::vector<std::uint8_t>{1, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 128, 255, 255, 0, 128, 255, 128}));
    for (const auto &[type, invalid] : {
             std::pair{"u8", "256"}, {"u8", "-1"}, {"u16", "65536"}, {"u32", "4294967296"}, {"i8", "-129"}, {"i8", "128"}, {"i16", "-32769"}, {"i16", "32768"}, {"i32", "-2147483649"}, {"i32", "2147483648"}, {"compact", "2147483648"}, {"padding", "256"}})
    {
        const auto definition = std::string(R"({"blocks":[{"fields":[{"name":"v","type":")") + type + R"(")" + (std::string_view(type) == "padding" ? R"(,"size":2)" : "") + "}]}]}";
        const auto single = parse_schema(definition);
        EXPECT_REJECTS(assemble(std::string("[{\"v\":") + invalid + "}]", single), "overflow");
    }
}

TEST(Dat, NestedArrayBinaryLayout)
{
    const auto remaining = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"materials","type":"group","fields":[{"name":"count","type":"i32"},{"name":"entries","type":"group","count":"count","fields":[{"name":"id","type":"i32"},{"name":"count","type":"i32"}]}]},{"name":"special","type":"group","fields":[{"name":"count","type":"i32"},{"name":"extra","type":"i32"},{"name":"entries","type":"group","count":"count","fields":[{"name":"id","type":"i32"},{"name":"count","type":"i32"}]}]},{"name":"mesh","type":"group","fields":[{"name":"meshCount","type":"i32"},{"name":"meshes","type":"group","count":"meshCount","fields":[{"name":"name","type":"utf16"},{"name":"bytes","type":"u8","count":2}]},{"name":"textureCount","type":"i32"},{"name":"textures","type":"utf16","count":"textureCount"},{"name":"extraTexture","type":"utf16"}]}]}]})");
    const std::vector<std::uint8_t> bytes = {
        2, 0, 0, 0,
        2, 0, 0, 0, 255, 255, 255, 255, 254, 255, 255, 255, 0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55,
        1, 0, 0, 0, 247, 255, 255, 255, 7, 0, 0, 0, 248, 255, 255, 255,
        1, 0, 0, 0, 2, 0, 0, 0, 'M', 0, 5, 250, 1, 0, 0, 0, 2, 0, 0, 0, 'T', 0, 2, 0, 0, 0, 'E', 0,
        0, 0, 0, 0, 0, 0, 0, 0, 246, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    const auto text = disassemble(bytes, remaining);
    EXPECT_EQ(assemble(text, remaining), bytes);
    for (std::size_t size = 0; size < bytes.size(); ++size)
        EXPECT_REJECTS(disassemble(std::span(bytes).first(size), remaining), "");
    auto negative_count = bytes;
    std::fill_n(negative_count.begin() + 4, 4, 255);
    EXPECT_REJECTS(disassemble(negative_count, remaining), "Count must be");
}

TEST(Dat, ConditionalFields)
{
    const auto conditional = ([]
                              {
                                  auto schema = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"flags","type":"u32","count":2},{"name":"kind","type":"i32"},{"name":"n","type":"u32"},{"name":"pad","type":"padding","size":3},{"name":"value","type":"u32","enabledBy":[[{"field":"flags","value":2,"mask":2},{"field":"kind","value":3}],[{"field":"kind","value":-1}]]},{"name":"items","type":"utf16","count":"n","enabledBy":[[{"field":"flags","value":1,"mask":1}]]},{"name":"tail","type":"u32"}]}]})");
                                  return schema; }());
    const std::vector<std::uint8_t> bytes = {
        4, 0, 0, 0,
        2, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0xa1, 0x86, 1, 0, 170, 170, 170, 77, 0, 0, 0, 9, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 88, 0, 0, 0, 10, 0, 0, 0,
        1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 255, 255, 255, 2, 0, 0, 0, 'A', 0, 2, 0, 0, 0, 'B', 0, 11, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 12, 0, 0, 0,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    const auto text = disassemble(bytes, conditional);
    EXPECT_EQ(assemble(text, conditional), bytes);
    auto mixed_padding = bytes;
    mixed_padding[21] = 171;
    EXPECT_REJECTS(disassemble(mixed_padding, conditional), "Nonuniform padding");
}
