#include "support.h"

using namespace l2asmdisasm;

TEST(Text, Latin1)
{
    const auto schema = parse_schema(R"({"trailer":false,"blocks":[{"fields":[{"name":"value","type":"string","terminated":false}]}]})");
    const std::vector<std::uint8_t> bytes = {1, 0, 0, 0, 4, 0xc9, 0x80, 0x9f, 0xff};
    EXPECT_EQ(assemble(disassemble(bytes, schema), schema), bytes);
    EXPECT_EQ(assemble(R"([{"value":"\u00e9"}])", schema), (std::vector<std::uint8_t>{1, 0, 0, 0, 1, 0xe9}));
    EXPECT_REJECTS(assemble(R"([{"value":"\u042f"}])", schema), "ISO-8859-1");
}
