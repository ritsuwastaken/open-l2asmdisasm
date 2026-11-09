#include "support.h"

using namespace l2asmdisasm;

TEST(RecordCount, Field)
{
    const auto schema = parse_schema(R"({"blocks":[{"recordCount":{"nonzero":"on"},"fields":[{"name":"id","type":"u8"},{"name":"on","type":"u32"}]}]})");
    const auto bytes = assemble(R"([{"id":1,"on":1},{"id":2,"on":0},{"id":3,"on":5}])", schema);
    EXPECT_EQ(bytes[0], 2);
    EXPECT_EQ(bytes.size(), 4u + 15 + 13);
    EXPECT_EQ(assemble(disassemble(bytes, schema), schema), bytes);
    EXPECT_EQ(schema.blocks[0].nonzero, "on");
    auto miscounted = bytes;
    miscounted[0] = 3;
    EXPECT_REJECTS(disassemble(miscounted, schema), "nonzero count");
}

TEST(RecordCount, Compact)
{
    const auto schema = parse_schema(R"({"safePackage":false,"blocks":[{"recordCount":"compact","fields":[{"name":"v","type":"u8"}]}]})");
    const auto bytes = assemble(R"([{"v":1},{"v":2}])", schema);
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{2, 1, 2}));
    EXPECT_EQ(assemble(disassemble(bytes, schema), schema), bytes);
    EXPECT_REJECTS(disassemble(std::vector<std::uint8_t>{1, 1, 2}, schema), "trailing DAT bytes");
    EXPECT_REJECTS(disassemble(std::vector<std::uint8_t>{0x40}, schema), "Truncated compact index");
    EXPECT_REJECTS(disassemble(std::vector<std::uint8_t>{0x81}, schema), "Negative compact record count");
}

TEST(RecordCount, Fixed)
{
    const auto schema = parse_schema(R"({"blocks":[{"recordCount":1,"fields":[{"name":"v","type":"u8"}]}]})");
    const auto bytes = assemble(R"([{"v":42}])", schema);
    EXPECT_EQ(bytes.size(), 14u);
    EXPECT_EQ(bytes[0], 42);
    EXPECT_EQ(bytes[1], 12);
    EXPECT_EQ(assemble(disassemble(bytes, schema), schema), bytes);
    EXPECT_REJECTS(assemble("[]", schema), "record count");
    auto bad_marker = bytes;
    bad_marker[1] = 1;
    EXPECT_REJECTS(disassemble(bad_marker, schema), "Invalid SafePackage marker");
    const auto empty = parse_schema(R"({"safePackage":false,"blocks":[{"recordCount":0,"fields":[{"name":"a","type":"u8"}]},{"recordCount":"compact","fields":[{"name":"b","type":"u16"}]}]})");
    EXPECT_EQ(assemble(R"({"blocks":[[],[{"b":8}]]})", empty), (std::vector<std::uint8_t>{1, 8, 0}));
    EXPECT_REJECTS(assemble(R"({"blocks":[[{"a":1}],[]]})", empty), "fixed recordCount");
}

TEST(Blocks, Layout)
{
    const auto schema = parse_schema(R"({"blocks":[{"fields":[{"name":"a","type":"u8"}]},{"recordCount":"eof","fields":[{"name":"b","type":"u8"}]}]})");
    ASSERT_EQ(schema.blocks.size(), 2u);
    EXPECT_EQ(schema.blocks[0].fields[0].name, "a");
    EXPECT_EQ(schema.blocks[1].fields[0].name, "b");
    EXPECT_REJECTS(assemble("[]", Schema{}), "no blocks");
    const auto bytes = assemble(R"({"blocks":[[{"a":1}],[{"b":2},{"b":3}]]})", schema);
    EXPECT_EQ(bytes.size(), 4u + 1 + 2 + 13);
    EXPECT_EQ(bytes[0], 1);
    EXPECT_EQ(assemble(disassemble(bytes, schema), schema), bytes);
    EXPECT_REJECTS(assemble(R"({"blocks":[[{"a":1}]]})", schema), "block count");
    EXPECT_REJECTS(assemble(R"({"blocks":[[{"a":1}],[{"wrong":2}]]})", schema), "Missing JSON property");
    EXPECT_REJECTS(assemble(R"({"blocks":[[{"a":1}],[],[]]})", schema), "block count");

    const auto counted = parse_schema(R"({"safePackage":false,"blocks":[{"recordCount":"compact","fields":[{"name":"id","type":"u8"}]},{"recordCount":{"nonzero":"flag"},"fields":[{"name":"flag","type":"u8"}]}]})");
    const auto mixed = assemble(R"({"blocks":[[{"id":7}],[{"flag":0},{"flag":1},{"flag":0}]]})", counted);
    EXPECT_EQ(mixed, (std::vector<std::uint8_t>{1, 7, 1, 0, 0, 0, 0, 1, 0}));
    EXPECT_EQ(assemble(disassemble(mixed, counted), counted), mixed);
}
