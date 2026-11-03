#include "support.h"

#include <algorithm>
#include <span>

using namespace l2asmdisasm;

namespace
{
const Schema &table()
{
    static const auto schema = parse_ddf(R"(
        # comment
        FS = "\t"; HEADER = YES; RECCNT = OFF;
        MTXCNT_OUT = YES; MATCNT_OUT = YES; ORD_IGNORE = NO;
        /* a block comment */
        { UINT id; UNICODE name; ASCF description; CHEX flags[2]; } // comment
    )");
    return schema;
}

const std::string table_text = "id\tname\tdescription\tflags[0]\tflags[1]\n17\tAΩ\ta,x\\0\\t\\\\\tFF\t0\n";
const std::vector<std::uint8_t> table_bytes = {
    1, 0, 0, 0, 17, 0, 0, 0, 4, 0, 0, 0, 0x41, 0, 0xa9, 3,
    4, 0x78, 0, 9, 0x5c, 0xff, 0,
    12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};

const Schema &compounds()
{
    static const auto schema = parse_ddf("HEADER=NO; { UINT n; UCHAR a[n]; SOFT=2; MTX mesh; SOFTM=1; SOFTT=1; MTX2 extra; SOFTM=1; SOFTT=1; }");
    return schema;
}

const std::string compound_text = "1\t7\t\t1\tM\t0\t\t1\tU\t5\t16909060\t0\t\n";
const std::vector<std::uint8_t> compound_bytes = {
    1, 0, 0, 0, 1, 0, 0, 0, 7,
    1, 0, 0, 0, 2, 0, 0, 0, 'M', 0, 0, 0, 0, 0,
    1, 0, 0, 0, 2, 0, 0, 0, 'U', 0, 5, 4, 3, 2, 1, 0, 0, 0, 0,
    12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
} // namespace

TEST(Dat, KnownVector)
{
    EXPECT_EQ(assemble(table_text, table()), table_bytes);
    EXPECT_EQ(disassemble(table_bytes, table()), table_text);
}

TEST(Dat, RejectsMalformedVector)
{
    const auto &schema = table();
    for (std::size_t size = 0; size < table_bytes.size(); ++size)
        EXPECT_REJECTS(disassemble(std::span(table_bytes).first(size), schema), "");
    auto malformed = table_bytes;
    malformed.push_back(0);
    EXPECT_REJECTS(disassemble(malformed, schema), "trailing bytes");
    malformed = table_bytes;
    malformed[0] = 0xff;
    EXPECT_REJECTS(disassemble(malformed, schema), "Record count");
    malformed = table_bytes;
    malformed[8] = 0xff;
    EXPECT_REJECTS(disassemble(malformed, schema), "Record 1, field name, byte 8");
    malformed = table_bytes;
    malformed[12] = 0;
    malformed[13] = 0xd8;
    EXPECT_REJECTS(disassemble(malformed, schema), "surrogate");
}

TEST(Dat, Header)
{
    const auto &schema = table();
    EXPECT_REJECTS(assemble("wrong\theader\n", schema), "columns");
    EXPECT_REJECTS(assemble("id\tname\tdescription\tflags[1]\tflags[0]\n", schema), "header");
    EXPECT_REJECTS(assemble("", schema), "Missing text header");
    const std::string empty = "id\tname\tdescription\tflags[0]\tflags[1]\n";
    EXPECT_EQ(disassemble(assemble(empty, schema), schema), empty);
    EXPECT_REJECTS(assemble("a\n1\n", parse_ddf("{ UCHAR a; UCHAR b; }")), "column 2 'b' is missing");
    EXPECT_REJECTS(assemble("a\tb\tc\n", parse_ddf("{ UCHAR a; UCHAR b; }")), "column 3 'c' is not in the DDF");
}

TEST(Dat, Strings)
{
    const auto strings = parse_ddf("HEADER=NO; { UNICODE name; ASCF value; }");
    EXPECT_ROUNDTRIP(strings, "汉字😀\\0\tu,A😀\\0\n");
    EXPECT_ROUNDTRIP(strings, "\ta,\n\ta,\\0\n\ta,éÿ\\n\\r\\t\\\\\n\ta,unterminated\n");
    const auto wide = assemble("\tu,A😀\n", strings);
    EXPECT_TRUE(wide[8] == 0x83 && wide[9] == 0x41 && wide[11] == 0x3d && wide[12] == 0xd8) << "ASCF length is not UTF-16 code units";
    EXPECT_REJECTS(assemble("\ta,汉\n", strings), "ISO-8859-1");
    EXPECT_REJECTS(assemble("\tu,\n", strings), "Empty wide ASCF");
    EXPECT_REJECTS(assemble("\ta,\\z\n", strings), "Unknown text escape");
    EXPECT_REJECTS(assemble("\ta,\\\n", strings), "Unterminated text escape");
    EXPECT_REJECTS(assemble(std::string("\ta,") + char(0xff) + "\n", strings), "Invalid UTF-8");
}

TEST(Dat, Numbers)
{
    const auto numbers = parse_ddf("HEADER=NO; { UINT a; INT b; UWORD c; WORD d; UCHAR e; CHAR f; HEX g; CHEX h; CNTR i; FLOAT j; }");
    EXPECT_ROUNDTRIP(numbers, "4294967295\t-2147483648\t65535\t-32768\t255\t-128\t0xdeadbeef\t0xff\t-2147483648\tNaN(0xffa12345)\n");
    EXPECT_ROUNDTRIP(numbers, "0\t2147483647\t0\t32767\t0\t127\t0x0\t0x0\t2147483647\t-0\n");
    for (const auto value : {"1.40129846e-45", "3.40282347e+38", "inf", "-inf", "1.25"})
        EXPECT_ROUNDTRIP(parse_ddf("HEADER=NO; { FLOAT value; }"), std::string(value) + "\n");
    const auto narrow = parse_ddf("HEADER=NO; { UCHAR a; WORD b; }");
    EXPECT_REJECTS(assemble("256\t0\n", narrow), "overflow");
    EXPECT_REJECTS(assemble("0\t-32769\n", narrow), "overflow");
    EXPECT_REJECTS(assemble("-1\t0\n", narrow), "integer");
    EXPECT_REJECTS(assemble("0\n", narrow), "columns");
}

TEST(Dat, Compounds)
{
    EXPECT_EQ(assemble(compound_text, compounds()), compound_bytes);
    EXPECT_EQ(disassemble(compound_bytes, compounds()), compound_text);
    Schema sized;
    const auto inferred = disassemble(compound_bytes, parse_ddf("HEADER=NO; { UINT n; UCHAR a[n]; MTX mesh; MTX2 extra; }"), &sized);
    EXPECT_EQ(sized.fields[1].soft, 1);
    EXPECT_EQ(sized.fields[3].softm, 1);
    EXPECT_EQ(sized.fields[3].softt, 1);
    EXPECT_EQ(assemble(inferred, parse_ddf(export_ddf(sized))), compound_bytes);
}

TEST(Dat, MaterialsAndMtx3)
{
    const auto remaining = parse_ddf("HEADER=NO; { MAT materials; MAT2 special; MTX3 mesh; }");
    const std::vector<std::uint8_t> bytes = {
        2, 0, 0, 0,
        2, 0, 0, 0, 255, 255, 255, 255, 254, 255, 255, 255, 0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55,
        1, 0, 0, 0, 247, 255, 255, 255, 7, 0, 0, 0, 248, 255, 255, 255,
        1, 0, 0, 0, 2, 0, 0, 0, 'M', 0, 5, 250, 1, 0, 0, 0, 2, 0, 0, 0, 'T', 0, 2, 0, 0, 0, 'E', 0,
        0, 0, 0, 0, 0, 0, 0, 0, 246, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    Schema sized;
    const auto text = disassemble(bytes, remaining, &sized);
    EXPECT_EQ(text, "2\t-1\t-2\t287454020\t1432778632\t1\t-9\t7\t-8\t1\tM\t5\t250\t1\tT\tE\n0\t\t\t\t\t0\t-10\t\t\t0\t\t\t\t0\t\t\n");
    EXPECT_EQ(assemble(text, parse_ddf(export_ddf(sized))), bytes);
    for (std::size_t size = 0; size < bytes.size(); ++size)
        EXPECT_REJECTS(disassemble(std::span(bytes).first(size), remaining), "");
    auto negative_count = bytes;
    std::fill_n(negative_count.begin() + 4, 4, 255);
    EXPECT_REJECTS(disassemble(negative_count, remaining), "Array count");
}

TEST(Dat, ConditionalFields)
{
    const auto conditional = parse_ddf(R"({
        UINT flags[2]; INT kind; UINT n; FILLER pad{3};
        UINT value; ENBBY=[(flags:2,2),(kind,3)]; ENBBY=[(kind,-1)];
        UNICODE items[n]; SOFT=1; ENBBY=[(flags:1,1)]; UINT tail;
    })");
    const std::vector<std::uint8_t> bytes = {
        4, 0, 0, 0,
        2, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0xa1, 0x86, 1, 0, 170, 170, 170, 77, 0, 0, 0, 9, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 88, 0, 0, 0, 10, 0, 0, 0,
        1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 255, 255, 255, 2, 0, 0, 0, 'A', 0, 2, 0, 0, 0, 'B', 0, 11, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 12, 0, 0, 0,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    Schema sized;
    const auto text = disassemble(bytes, conditional, &sized);
    EXPECT_EQ(text, "flags[0]\tflags[1]\tkind\tn\tpad{3}\tvalue\titems[0]\titems[1]\ttail\n2\t0\t3\t100001\t170\t77\t\t\t9\n0\t0\t-1\t0\t0\t88\t\t\t10\n1\t0\t0\t2\t255\t\tA\tB\t11\n0\t0\t3\t1\t1\t\t\t\t12\n");
    EXPECT_EQ(assemble(text, parse_ddf(export_ddf(sized))), bytes);
    auto mixed_filler = bytes;
    mixed_filler[21] = 171;
    EXPECT_REJECTS(disassemble(mixed_filler, conditional), "Nonuniform FILLER");
    EXPECT_ROUNDTRIP(parse_ddf("HEADER=NO; { UINT flag; UINT optional; ENBBY=[(flag,-1)]; }"), "4294967295\t5\n");
    EXPECT_ROUNDTRIP(parse_ddf("HEADER=NO; { HEX flag; UCHAR value; ENBBY=[(flag:128,128)]; }"), "80\t7\n");
    EXPECT_ROUNDTRIP(parse_ddf("HEADER=NO; { UCHAR flag; MAT value; SOFTM=1; ENBBY=[(flag,1)]; }"), "1\t0\t\t\n0\t\t\t\n");
    EXPECT_ROUNDTRIP(parse_ddf("HEADER=NO; { UCHAR flag; UINT first; ENBBY=[(flag,1)]; UINT next; ENBBY=[(first,0)]; }"), "1\t0\t7\n0\t\t9\n");
    EXPECT_REJECTS(assemble("0\t7\n", parse_ddf("HEADER=NO; { UINT flag; UCHAR value; ENBBY=[(flag,1)]; }")), "disabled field");
    EXPECT_REJECTS(assemble("256\n", parse_ddf("HEADER=NO; { FILLER pad{3}; }")), "FILLER byte overflow");
}

TEST(Dat, Presentation)
{
    auto presentation = parse_ddf("{ UINT a; UINT hidden; ORD=OFF; INT vals[4]; SKIPIF=[(0,3),(1 .. 2,-4 .. -2)]; ORD=2; FLOAT f[3]; SKIPIF=[(0 .. 1,1.25 .. 2.5)]; ORD=1; UINT tied; ORD=2; UINT z; }");
    const std::vector<std::uint8_t> bytes = {
        1, 0, 0, 0, 10, 0, 0, 0, 99, 0, 0, 0,
        3, 0, 0, 0, 252, 255, 255, 255, 255, 255, 255, 255, 8, 0, 0, 0,
        0, 0, 160, 63, 0, 0, 32, 64, 0, 0, 96, 64, 20, 0, 0, 0, 30, 0, 0, 0,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    const auto presented = disassemble(bytes, presentation);
    EXPECT_EQ(presented, "f[0]\tf[1]\tf[2]\tvals[0]\tvals[1]\tvals[2]\tvals[3]\ttied\ta\tz\n\t\t3.5\t\t\t-1\t8\t20\t10\t30\n");
    EXPECT_EQ(disassemble(bytes, parse_ddf(export_ddf(presentation))), presented);
    EXPECT_REJECTS(assemble(presented, presentation), "columns");
    presentation.ignore_order = true;
    const auto ignored = disassemble(bytes, presentation);
    EXPECT_EQ(ignored, "a\thidden\tvals[0]\tvals[1]\tvals[2]\tvals[3]\tf[0]\tf[1]\tf[2]\ttied\tz\n10\t99\t\t\t-1\t8\t\t\t3.5\t20\t30\n");
    auto zeroed = bytes;
    for (const auto offset : {12, 16, 28, 32})
        std::fill_n(zeroed.begin() + offset, 4, 0);
    EXPECT_EQ(assemble(ignored, presentation), zeroed) << "Assembly must ignore ORD/SKIPIF and encode blank numerics as zero";
    auto ordered_compounds = compounds();
    ordered_compounds.fields[2].order = ordered_compounds.fields[3].order = 0;
    EXPECT_EQ(disassemble(compound_bytes, ordered_compounds), "1\tM\t0\t\t1\tU\t5\t16909060\t0\t\t1\t7\t\n") << "ORD split a compound or reordered ties";
    EXPECT_REJECTS(assemble("b\ta\n2\t1\n", parse_ddf("{ UINT a; UINT b; ORD=0; }")), "header");
}

TEST(Dat, SkipIf)
{
    const std::vector<std::uint8_t> fraction_bytes = {1, 0, 0, 0, 205, 204, 204, 61, 205, 204, 76, 62, 205, 204, 204, 189, 12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    const auto fraction = parse_ddf("HEADER=NO; { FLOAT a[3]; SKIPIF=[(0,0.1),(1,0.2),(2,-0.1)]; }");
    EXPECT_EQ(disassemble(fraction_bytes, parse_ddf(export_ddf(fraction))), "\t\t\n") << "SKIPIF must compare float32 values";
    const std::vector<std::uint8_t> unsigned_bytes = {1, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    EXPECT_EQ(disassemble(unsigned_bytes, parse_ddf("HEADER=NO; { UINT a; SKIPIF=[(0,0 .. 4294967295)]; INT b; SKIPIF=[(0,4294967295)]; UWORD c; SKIPIF=[(0,-1)]; }")), "4294967295\t\t\n");
    const auto skip_nan = parse_ddf("HEADER=NO; { FLOAT a; SKIPIF=[(0,-3.40282347e38 .. 3.40282347e38)]; }");
    EXPECT_EQ(disassemble(assemble("NaN(0x7fc12345)\n", skip_nan), skip_nan), "NaN(0x7FC12345)\n");
    const auto skip_control = parse_ddf("HEADER=NO; { UINT flag; SKIPIF=[(0,1)]; UINT value; ENBBY=[(flag,1)]; }");
    EXPECT_EQ(disassemble(assemble("1\t7\n", skip_control), skip_control), "\t7\n") << "SKIPIF changed ENBBY input";
}

TEST(Dat, DynamicArrays)
{
    const auto dynamic = parse_ddf("HEADER=NO; { INT n; UINT a[n]; SOFT=1; }");
    EXPECT_REJECTS(assemble("-1\t\n", dynamic), "Array count");
    EXPECT_REJECTS(assemble("2\t7\n", dynamic), "SOFT");
    EXPECT_REJECTS(assemble("0\t7\n", dynamic), "padding");
}

TEST(Dat, CountersOut)
{
    const auto counts_off = parse_ddf("HEADER=NO; MTXCNT_OUT=NO; MATCNT_OUT=NO; { MTX mesh; SOFTM=2; SOFTT=2; MAT mats; SOFTM=2; MAT2 special; SOFTM=1; }");
    const std::vector<std::uint8_t> bytes = {
        1, 0, 0, 0,
        1, 0, 0, 0, 2, 0, 0, 0, 'M', 0, 1, 0, 0, 0, 2, 0, 0, 0, 'T', 0,
        2, 0, 0, 0, 10, 0, 0, 0, 20, 0, 0, 0, 30, 0, 0, 0, 40, 0, 0, 0,
        1, 0, 0, 0, 99, 0, 0, 0, 50, 0, 0, 0, 60, 0, 0, 0,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    const std::string suppressed = "M\t\tT\t\t10\t20\t30\t40\t99\t50\t60\n";
    EXPECT_EQ(disassemble(bytes, counts_off), suppressed);
    EXPECT_EQ(disassemble(bytes, parse_ddf(export_ddf(counts_off))), suppressed);
    const auto counts_on = parse_ddf("HEADER=NO; { MTX mesh; SOFTM=2; SOFTT=2; MAT mats; SOFTM=2; MAT2 special; SOFTM=1; }");
    const auto text = disassemble(bytes, counts_on);
    EXPECT_EQ(text, "1\tM\t\t1\tT\t\t2\t10\t20\t30\t40\t1\t99\t50\t60\n");
    EXPECT_EQ(assemble(text, counts_off), bytes) << "Assembly must ignore CNT_OUT=NO and require counters";
    EXPECT_REJECTS(assemble(suppressed, counts_off), "columns");
}

TEST(Dat, FieldReferences)
{
    const std::vector<std::uint8_t> duplicate_bytes = {
        1, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 7, 0, 0, 0, 9, 0, 0, 0,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    const auto duplicates = parse_ddf("{ UINT n; UINT n; UINT b[n]; SOFT=1; UINT e; ENBBY=[(n,1)]; }");
    EXPECT_EQ(disassemble(duplicate_bytes, duplicates), "n\tn\tb\te\n1\t2\t7\t9\n");
    EXPECT_EQ(assemble(disassemble(duplicate_bytes, duplicates), duplicates), duplicate_bytes);
    const std::vector<std::uint8_t> array_counter_bytes = {
        1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 9, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    Schema array_counter = parse_ddf("HEADER=NO; { UINT c; UINT e[c]; UINT x[e]; }");
    EXPECT_EQ(disassemble(array_counter_bytes, array_counter, &array_counter), "2\t3\t9\t1\t2\t3\n");
    EXPECT_EQ(assemble("2\t3\t9\t1\t2\t3\n", parse_ddf(export_ddf(array_counter))), array_counter_bytes);
}

TEST(Dat, FieldSeparator)
{
    const auto comma = parse_ddf("FS=\",\"; HEADER=YES; { UINT id; UNICODE name; UINT a; UINT b; }");
    const std::vector<std::uint8_t> bytes = {
        1, 0, 0, 0, 7, 0, 0, 0, 10, 0, 0, 0, 'H', 0, 'e', 0, 'l', 0, 'l', 0, 'o', 0, 1, 0, 0, 0, 2, 0, 0, 0,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    EXPECT_EQ(disassemble(bytes, comma), "id,name,a,b\n7,Hello,1,2\n");
    EXPECT_EQ(parse_ddf(export_ddf(comma)).separator, ",");
    EXPECT_EQ(disassemble(bytes, parse_ddf("FS=\" | \"; HEADER=NO; { UINT id; UNICODE name; UINT a; UINT b; }")), "7 | Hello | 1 | 2\n");
    const auto tabbed = parse_ddf("FS=\"x\\ty\"; HEADER=NO; { UINT id; UNICODE name; }");
    EXPECT_EQ(tabbed.separator, "x\ty");
    EXPECT_EQ(parse_ddf(export_ddf(tabbed)).separator, "x\ty");
    EXPECT_EQ(assemble("id\tname\ta\tb\n7\tHello\t1\t2\n", comma), bytes) << "Assembly must ignore non-tab FS";
    EXPECT_REJECTS(assemble("id,name,a,b\n7,Hello,1,2\n", comma), "columns");
}

TEST(Dat, Groups)
{
    const auto groups = parse_ddf("HEADER=YES; { UINT id; UCHAR n; { UINT mesh; CNTR t; UINT tex[t]; } drop[n]; UCHAR tail; ENBBY=[(n,0)]; }");
    const std::vector<std::uint8_t> bytes = {
        2, 0, 0, 0,
        7, 0, 0, 0, 2, 1, 0, 0, 0, 2, 5, 0, 0, 0, 6, 0, 0, 0, 9, 0, 0, 0, 0,
        8, 0, 0, 0, 0, 3,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    Schema sized;
    const auto text = disassemble(bytes, groups, &sized);
    EXPECT_EQ(text, "id\tn\tdrop[0].mesh\tdrop[0].t\tdrop[0].tex[0]\tdrop[0].tex[1]\tdrop[1].mesh\tdrop[1].t\tdrop[1].tex[0]\tdrop[1].tex[1]\ttail\n"
                    "7\t2\t1\t2\t5\t6\t9\t0\t\t\t\n"
                    "8\t0\t\t\t\t\t\t\t\t\t3\n");
    EXPECT_EQ(sized.fields[2].soft, 2u);
    EXPECT_EQ(sized.fields[2].members[2].soft, 2u);
    EXPECT_EQ(assemble(text, parse_ddf(export_ddf(sized))), bytes);
    EXPECT_EQ(export_ddf(parse_ddf(export_ddf(sized))), export_ddf(sized));
    EXPECT_REJECTS(assemble("id\tn\tdrop[0].mesh\tdrop[0].t\tdrop[0].tex[0]\tdrop[0].tex[1]\tdrop[1].mesh\tdrop[1].t\tdrop[1].tex[0]\tdrop[1].tex[1]\ttail\n"
                            "8\t0\t1\t\t\t\t\t\t\t\t3\n",
                            parse_ddf(export_ddf(sized))),
                   "Nonempty padding or disabled field");

    const auto nested = parse_ddf("HEADER=NO; { UCHAR k; { UCHAR k; UCHAR v[k]; UCHAR o; ENBBY=[(k,1)]; } g[2]; { UCHAR a; } s; }");
    const std::vector<std::uint8_t> nested_bytes = {1, 0, 0, 0, 3, 1, 4, 5, 2, 6, 7, 9, 12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    EXPECT_EQ(disassemble(nested_bytes, nested, &sized), "3\t1\t4\t\t5\t2\t6\t7\t\t9\n");
    EXPECT_EQ(assemble("3\t1\t4\t\t5\t2\t6\t7\t\t9\n", sized), nested_bytes);
    const auto outer = parse_ddf("HEADER=NO; { UCHAR n; UCHAR f; { UCHAR c; { UCHAR x; ENBBY=[(f,1)]; } h[n]; } g[n]; }");
    const std::vector<std::uint8_t> outer_bytes = {2, 0, 0, 0, 2, 1, 1, 2, 3, 4, 5, 6, 1, 0, 7, 12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    const std::string outer_text = "2\t1\t1\t2\t3\t4\t5\t6\n1\t0\t7\t\t\t\t\t\n";
    EXPECT_EQ(disassemble(outer_bytes, outer, &sized), outer_text);
    EXPECT_EQ(assemble(outer_text, sized), outer_bytes);
}

TEST(Dat, SixtyFourBit)
{
    const auto wide = parse_ddf("HEADER=NO; { INT64 a; DOUBLE b; UINT n; INT64 c[n]; }");
    const std::vector<std::uint8_t> bytes = {
        1, 0, 0, 0,
        0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0xf8, 0x3f, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    Schema sized;
    EXPECT_EQ(disassemble(bytes, wide, &sized), "-2\t1.5\t1\t-9223372036854775808\n");
    EXPECT_EQ(assemble("-2\t1.5\t1\t-9223372036854775808\n", sized), bytes);
    EXPECT_ROUNDTRIP(parse_ddf("HEADER=NO; { INT64 a; DOUBLE b; }"), "9223372036854775807\tNaN(0x7FF8000000000001)\n");
    EXPECT_ROUNDTRIP(parse_ddf("HEADER=NO; { DOUBLE b; }"), "-0\n4.9406564584124654e-324\ninf\n");
    EXPECT_REJECTS(assemble("9223372036854775808\t0\n", parse_ddf("HEADER=NO; { INT64 a; DOUBLE b; }")), "integer");
    EXPECT_REJECTS(assemble("0\tNaN(0x1)\n", parse_ddf("HEADER=NO; { INT64 a; DOUBLE b; }")), "NaN");
    EXPECT_REJECTS(parse_ddf("{ INT64 a; SKIPIF=[(0,0)]; }"), "SKIPIF");
}
