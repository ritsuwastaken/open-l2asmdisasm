#include "support.h"

using namespace l2asmdisasm;

TEST(RecordCount, Field)
{
    const auto flagged = parse_ddf("HEADER=NO; RECCNT=on; { UCHAR id; UINT on; }");
    const auto bytes = assemble("1\t1\n2\t0\n3\t5\n", flagged);
    EXPECT_EQ(bytes[0], 2);
    EXPECT_EQ(bytes.size(), 4u + 15 + 13);
    EXPECT_EQ(disassemble(bytes, flagged), "1\t1\n2\t0\n3\t5\n");
    EXPECT_NE(export_ddf(flagged).find("RECCNT = on;"), std::string::npos);
    auto miscounted = bytes;
    miscounted[0] = 3;
    EXPECT_REJECTS(disassemble(miscounted, flagged), "differs from the 2 records");
    EXPECT_REJECTS(parse_ddf("RECCNT=missing; { UINT a; }"), "RECCNT must name an integer field");
    EXPECT_ROUNDTRIP(parse_ddf("HEADER=NO; RECCNT=_on; { UCHAR id; UINT _on; }"), "1\t1\n2\t0\n");
}

TEST(RecordCount, Compact)
{
    const auto compact = parse_ddf("HEADER=NO; RECCNT=CNTR; TRAILER=NO; { UCHAR v; }");
    const auto bytes = assemble("1\n2\n", compact);
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{2, 1, 2}));
    EXPECT_EQ(disassemble(bytes, compact), "1\n2\n");
    EXPECT_REJECTS(disassemble(std::vector<std::uint8_t>{1, 1, 2}, compact), "trailing bytes");
    EXPECT_REJECTS(disassemble(std::vector<std::uint8_t>{0x40}, compact), "Record count, byte 0: Truncated compact index");
}

TEST(RecordCount, FixedWithMagic)
{
    const auto fixed = parse_ddf("HEADER=NO; RECCNT=1; MAGIC=2; { UCHAR v; }");
    const auto bytes = assemble("42\n", fixed);
    EXPECT_EQ(bytes.size(), 22u);
    EXPECT_EQ(bytes[0], 42);
    EXPECT_EQ(bytes[9], 12);
    EXPECT_EQ(disassemble(bytes, fixed), "42\n");
    EXPECT_REJECTS(assemble("", fixed), "RECCNT");
    auto bad_magic = bytes;
    bad_magic[1] = 1;
    EXPECT_REJECTS(disassemble(bad_magic, fixed), "MAGIC");
}

TEST(Blocks, Layout)
{
    const auto blocks = parse_ddf("RECCNT=OFF; { UCHAR a; } RECCNT=EOF; { UCHAR b; }");
    const std::string text = "a\n1\n\\block\nb\n2\n3\n";
    const auto bytes = assemble(text, blocks);
    EXPECT_EQ(bytes.size(), 4u + 1 + 2 + 13);
    EXPECT_EQ(bytes[0], 1);
    EXPECT_EQ(disassemble(bytes, blocks), text);
    EXPECT_EQ(export_ddf(parse_ddf(export_ddf(blocks))), export_ddf(blocks));
    EXPECT_REJECTS(assemble("a\n1\n\\block\n", blocks), "Missing text header of block 2");
    EXPECT_REJECTS(assemble("a\n1\n\\block\nwrong\n", blocks), "Text line 4: header does not match DDF columns; column 1 is 'wrong', expected 'b'");
    EXPECT_REJECTS(assemble(text + "\\block\n", blocks), "Unexpected text block marker");
}

TEST(Blocks, HeaderLikeStrings)
{
    for (const auto count : {"1", "OFF", "CNTR"})
    {
        const auto collision = parse_ddf(std::string("RECCNT=") + count + "; { UNICODE a; } RECCNT=1; { UCHAR b; }");
        const std::string text = "a\nb\n\\block\nb\n1\n";
        const auto bytes = assemble(text, collision);
        EXPECT_EQ(disassemble(bytes, collision), text) << count;
        EXPECT_EQ(assemble(disassemble(bytes, collision), collision), bytes) << count;
    }
    EXPECT_ROUNDTRIP(parse_ddf("{ UNICODE a; } { UNICODE a; }"), "a\n\\\\block\n\\block\na\na\n");
    EXPECT_ROUNDTRIP(parse_ddf("RECCNT=0; { UINT a; } RECCNT=0; { UINT b; } { UINT c; }"), "a\n\\block\nb\n\\block\nc\n");
}

TEST(Blocks, Rejects)
{
    EXPECT_REJECTS(assemble("a\n\\block\nb\n1\n", parse_ddf("RECCNT=1; { UNICODE a; } { UCHAR b; }")), "Text record count differs");
    EXPECT_REJECTS(assemble("a\n1\n", parse_ddf("{ UCHAR a; } RECCNT=EOF; { UCHAR b; }")), "Missing text header of block 2");
    EXPECT_REJECTS(parse_ddf("{ UCHAR a; } HEADER=NO; { UCHAR b; }"), "Only RECCNT may precede");
    EXPECT_REJECTS(parse_ddf("{ UINT a; UINT b; } FS=\",\"; { UINT c; UINT d; }"), "Only RECCNT may precede");
    EXPECT_REJECTS(disassemble(std::vector<std::uint8_t>{1}, parse_ddf("RECCNT=EOF; { UCHAR a; } { UCHAR b; }")), "Only the last block");
}
