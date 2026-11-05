#include "support.h"

using namespace l2asmdisasm;

TEST(Text, Charset)
{
    const auto schema = parse_schema(R"({"trailer":false,"blocks":[{"fields":[{"name":"value","type":"string","terminated":false}]}]})");
    EncodingOptions cyrillic;
    cyrillic.charset = "windows-1251";
    const auto bytes = assemble(R"([{"value":"Ая€"}])", export_schema(schema), {}, cyrillic);
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{1, 0, 0, 0, 3, 0xc0, 0xff, 0x88}));
    EXPECT_EQ(assemble(disassemble(bytes, export_schema(schema), {}, cyrillic), schema, cyrillic), bytes);
    EXPECT_REJECTS(assemble(R"([{"value":"é"}])", schema, cyrillic), "CP1251");
    const std::vector<std::uint8_t> undefined = {1, 0, 0, 0, 1, 0x98};
    EXPECT_REJECTS(disassemble(undefined, schema, cyrillic), "undefined in CP1251");
    EncodingOptions unknown;
    unknown.charset = "EBCDIC";
    EXPECT_REJECTS(assemble("[]", schema, unknown), "Unsupported charset");
    EXPECT_EQ(supported_charsets().front(), "ISO-8859-1");
}
