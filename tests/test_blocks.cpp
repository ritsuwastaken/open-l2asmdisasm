#include "support.h"

using namespace l2asmdisasm;

TEST(RecordCount, Field)
{
    const auto flagged = tsv_schema(R"({"blocks":[{"recordCount":{"nonzero":"on"},"fields":[{"name":"id","type":"u8"},{"name":"on","type":"u32"}]}]})");
    const auto bytes = assemble_tsv("1\t1\n2\t0\n3\t5\n", flagged);
    EXPECT_EQ(bytes[0], 2);
    EXPECT_EQ(bytes.size(), 4u + 15 + 13);
    EXPECT_EQ(disassemble_tsv(bytes, flagged), "1\t1\n2\t0\n3\t5\n");
    EXPECT_EQ(parse_schema(export_schema(flagged)).counted_by, "on");
    auto miscounted = bytes;
    miscounted[0] = 3;
    EXPECT_REJECTS(disassemble_tsv(miscounted, flagged), "differs from the 2 records");
    EXPECT_ROUNDTRIP(tsv_schema(R"({"blocks":[{"recordCount":{"nonzero":"_on"},"fields":[{"name":"id","type":"u8"},{"name":"_on","type":"u32"}]}]})"),
                     "1\t1\n2\t0\n");
}

TEST(RecordCount, Compact)
{
    const auto compact = tsv_schema(R"({"trailer":false,"blocks":[{"recordCount":"compact","fields":[{"name":"v","type":"u8"}]}]})");
    const auto bytes = assemble_tsv("1\n2\n", compact);
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{2, 1, 2}));
    EXPECT_EQ(disassemble_tsv(bytes, compact), "1\n2\n");
    EXPECT_REJECTS(disassemble_tsv(std::vector<std::uint8_t>{1, 1, 2}, compact), "trailing bytes");
    EXPECT_REJECTS(disassemble_tsv(std::vector<std::uint8_t>{0x40}, compact), "Record count, byte 0: Truncated compact index");
}

TEST(RecordCount, FixedWithMagic)
{
    const auto fixed = tsv_schema(R"({"magic":2,"blocks":[{"recordCount":1,"fields":[{"name":"v","type":"u8"}]}]})");
    const auto bytes = assemble_tsv("42\n", fixed);
    EXPECT_EQ(bytes.size(), 22u);
    EXPECT_EQ(bytes[0], 42);
    EXPECT_EQ(bytes[9], 12);
    EXPECT_EQ(disassemble_tsv(bytes, fixed), "42\n");
    EXPECT_REJECTS(assemble_tsv("", fixed), "RECCNT");
    auto bad_magic = bytes;
    bad_magic[1] = 1;
    EXPECT_REJECTS(disassemble_tsv(bad_magic, fixed), "MAGIC");
}

TEST(Blocks, Layout)
{
    const auto blocks = parse_schema(R"({"blocks":[{"fields":[{"name":"a","type":"u8"}]},{"recordCount":"eof","fields":[{"name":"b","type":"u8"}]}]})");
    const std::string text = "a\n1\n\\block\nb\n2\n3\n";
    const auto bytes = assemble_tsv(text, blocks);
    EXPECT_EQ(bytes.size(), 4u + 1 + 2 + 13);
    EXPECT_EQ(bytes[0], 1);
    EXPECT_EQ(disassemble_tsv(bytes, blocks), text);
    EXPECT_REJECTS(assemble_tsv("a\n1\n\\block\n", blocks), "Missing text header of block 2");
    EXPECT_REJECTS(assemble_tsv("a\n1\n\\block\nwrong\n", blocks), "Text line 4: header does not match schema columns; column 1 is 'wrong', expected 'b'");
    EXPECT_REJECTS(assemble_tsv(text + "\\block\n", blocks), "Unexpected text block marker");
}

TEST(Blocks, HeaderLikeStrings)
{
    for (const auto count : {std::int64_t{1}, Block::stored, Block::compact})
    {
        const auto collision = parse_schema(R"({"blocks":[{"fields":[{"name":"a","type":"unicode"}]},{"recordCount":1,"fields":[{"name":"b","type":"u8"}]}]})");
        auto counted = collision;
        counted.record_count = count;
        const std::string text = "a\nb\n\\block\nb\n1\n";
        const auto bytes = assemble_tsv(text, counted);
        EXPECT_EQ(disassemble_tsv(bytes, counted), text) << count;
        EXPECT_EQ(assemble_tsv(disassemble_tsv(bytes, counted), counted), bytes) << count;
    }
    EXPECT_ROUNDTRIP(parse_schema(R"({"blocks":[{"fields":[{"name":"a","type":"unicode"}]},{"fields":[{"name":"a","type":"unicode"}]}]})"), "a\n\\\\block\n\\block\na\na\n");
    EXPECT_ROUNDTRIP(parse_schema(R"({"blocks":[{"recordCount":0,"fields":[{"name":"a","type":"u32"}]},{"recordCount":0,"fields":[{"name":"b","type":"u32"}]},{"fields":[{"name":"c","type":"u32"}]}]})"), "a\n\\block\nb\n\\block\nc\n");
}

TEST(Blocks, Rejects)
{
    EXPECT_REJECTS(assemble_tsv("a\n\\block\nb\n1\n", parse_schema(R"({"blocks":[{"recordCount":1,"fields":[{"name":"a","type":"unicode"}]},{"fields":[{"name":"b","type":"u8"}]}]})")), "Text record count differs");
    EXPECT_REJECTS(assemble_tsv("a\n1\n", parse_schema(R"({"blocks":[{"fields":[{"name":"a","type":"u8"}]},{"recordCount":"eof","fields":[{"name":"b","type":"u8"}]}]})")), "Missing text header of block 2");
}
