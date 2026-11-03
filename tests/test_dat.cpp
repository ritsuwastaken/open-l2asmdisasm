#include "support.h"

#include <algorithm>
#include <span>

using namespace l2asmdisasm;

namespace
{
const Schema &table()
{
    static const auto schema = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"id","type":"u32"},{"name":"name","type":"unicode"},{"name":"description","type":"ascf"},{"name":"flags","type":"hex8","count":2}]}]})");
    return schema;
}

const std::string table_text = "id\tname\tdescription\tflags[0]\tflags[1]\n17\tAΩ\ta,x\\0\\t\\\\\tFF\t0\n";
const std::vector<std::uint8_t> table_bytes = {
    1, 0, 0, 0, 17, 0, 0, 0, 4, 0, 0, 0, 0x41, 0, 0xa9, 3,
    4, 0x78, 0, 9, 0x5c, 0xff, 0,
    12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};

const Schema &compounds()
{
    static const auto schema = ([]
                                {
                                    auto schema = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"n","type":"u32"},{"name":"a","type":"u8","count":"n"},{"name":"mesh","type":"mtx"},{"name":"extra","type":"mtx2"}]}]})");
                                    schema.header = false;
                                    schema.fields[1].soft = 2;
                                    schema.fields[2].softm = 1;
                                    schema.fields[2].softt = 1;
                                    schema.fields[3].softm = 1;
                                    schema.fields[3].softt = 1;
                                    return schema;
                                }());
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
    EXPECT_EQ(assemble_tsv(table_text, table()), table_bytes);
    EXPECT_EQ(disassemble_tsv(table_bytes, table()), table_text);
}

TEST(Dat, RejectsMalformedVector)
{
    const auto &schema = table();
    for (std::size_t size = 0; size < table_bytes.size(); ++size)
        EXPECT_REJECTS(disassemble_tsv(std::span(table_bytes).first(size), schema), "");
    auto malformed = table_bytes;
    malformed.push_back(0);
    EXPECT_REJECTS(disassemble_tsv(malformed, schema), "trailing bytes");
    malformed = table_bytes;
    malformed[0] = 0xff;
    EXPECT_REJECTS(disassemble_tsv(malformed, schema), "Record count");
    malformed = table_bytes;
    malformed[8] = 0xff;
    EXPECT_REJECTS(disassemble_tsv(malformed, schema), "Record 1, field name, byte 8");
    malformed = table_bytes;
    malformed[12] = 0;
    malformed[13] = 0xd8;
    EXPECT_REJECTS(disassemble_tsv(malformed, schema), "surrogate");
}

TEST(Dat, Header)
{
    const auto &schema = table();
    EXPECT_REJECTS(assemble_tsv("wrong\theader\n", schema), "columns");
    EXPECT_REJECTS(assemble_tsv("id\tname\tdescription\tflags[1]\tflags[0]\n", schema), "header");
    EXPECT_REJECTS(assemble_tsv("", schema), "Missing text header");
    const std::string empty = "id\tname\tdescription\tflags[0]\tflags[1]\n";
    EXPECT_EQ(disassemble_tsv(assemble_tsv(empty, schema), schema), empty);
    EXPECT_REJECTS(assemble_tsv("a\n1\n", parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"a","type":"u8"},{"name":"b","type":"u8"}]}]})")), "column 2 'b' is missing");
    EXPECT_REJECTS(assemble_tsv("a\tb\tc\n", parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"a","type":"u8"},{"name":"b","type":"u8"}]}]})")), "column 3 'c' is not in the schema");
}

TEST(Dat, Strings)
{
    const auto strings = tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"name","type":"unicode"},{"name":"value","type":"ascf"}]}]})");
    EXPECT_ROUNDTRIP(strings, "汉字😀\\0\tu,A😀\\0\n");
    EXPECT_ROUNDTRIP(strings, "\ta,\n\ta,\\0\n\ta,éÿ\\n\\r\\t\\\\\n\ta,unterminated\n");
    const auto wide = assemble_tsv("\tu,A😀\n", strings);
    EXPECT_TRUE(wide[8] == 0x83 && wide[9] == 0x41 && wide[11] == 0x3d && wide[12] == 0xd8) << "ASCF length is not UTF-16 code units";
    EXPECT_REJECTS(assemble_tsv("\ta,汉\n", strings), "ISO-8859-1");
    EXPECT_REJECTS(assemble_tsv("\tu,\n", strings), "Empty wide ASCF");
    EXPECT_REJECTS(assemble_tsv("\ta,\\z\n", strings), "Unknown text escape");
    EXPECT_REJECTS(assemble_tsv("\ta,\\\n", strings), "Unterminated text escape");
    EXPECT_REJECTS(assemble_tsv(std::string("\ta,") + char(0xff) + "\n", strings), "Invalid UTF-8");
}

TEST(Dat, Numbers)
{
    const auto numbers = tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"a","type":"u32"},{"name":"b","type":"i32"},{"name":"c","type":"u16"},{"name":"d","type":"i16"},{"name":"e","type":"u8"},{"name":"f","type":"i8"},{"name":"g","type":"hex32"},{"name":"h","type":"hex8"},{"name":"i","type":"compact"},{"name":"j","type":"f32"}]}]})");
    EXPECT_ROUNDTRIP(numbers, "4294967295\t-2147483648\t65535\t-32768\t255\t-128\t0xdeadbeef\t0xff\t-2147483648\tNaN(0xffa12345)\n");
    EXPECT_ROUNDTRIP(numbers, "0\t2147483647\t0\t32767\t0\t127\t0x0\t0x0\t2147483647\t-0\n");
    for (const auto value : {"1.40129846e-45", "3.40282347e+38", "inf", "-inf", "1.25"})
        EXPECT_ROUNDTRIP(tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"value","type":"f32"}]}]})"), std::string(value) + "\n");
    const auto narrow = tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"a","type":"u8"},{"name":"b","type":"i16"}]}]})");
    EXPECT_REJECTS(assemble_tsv("256\t0\n", narrow), "overflow");
    EXPECT_REJECTS(assemble_tsv("0\t-32769\n", narrow), "overflow");
    EXPECT_REJECTS(assemble_tsv("-1\t0\n", narrow), "integer");
    EXPECT_REJECTS(assemble_tsv("0\n", narrow), "columns");
}

TEST(Dat, Compounds)
{
    EXPECT_EQ(assemble_tsv(compound_text, compounds()), compound_bytes);
    EXPECT_EQ(disassemble_tsv(compound_bytes, compounds()), compound_text);
    Schema sized;
    const auto inferred = disassemble_tsv(compound_bytes, tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"n","type":"u32"},{"name":"a","type":"u8","count":"n"},{"name":"mesh","type":"mtx"},{"name":"extra","type":"mtx2"}]}]})"), &sized);
    EXPECT_EQ(sized.fields[1].soft, 1);
    EXPECT_EQ(sized.fields[3].softm, 1);
    EXPECT_EQ(sized.fields[3].softt, 1);
    EXPECT_EQ(assemble_tsv(inferred, sized), compound_bytes);
}

TEST(Dat, MaterialsAndMtx3)
{
    const auto remaining = tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"materials","type":"mat"},{"name":"special","type":"mat2"},{"name":"mesh","type":"mtx3"}]}]})");
    const std::vector<std::uint8_t> bytes = {
        2, 0, 0, 0,
        2, 0, 0, 0, 255, 255, 255, 255, 254, 255, 255, 255, 0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55,
        1, 0, 0, 0, 247, 255, 255, 255, 7, 0, 0, 0, 248, 255, 255, 255,
        1, 0, 0, 0, 2, 0, 0, 0, 'M', 0, 5, 250, 1, 0, 0, 0, 2, 0, 0, 0, 'T', 0, 2, 0, 0, 0, 'E', 0,
        0, 0, 0, 0, 0, 0, 0, 0, 246, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    Schema sized;
    const auto text = disassemble_tsv(bytes, remaining, &sized);
    EXPECT_EQ(text, "2\t-1\t-2\t287454020\t1432778632\t1\t-9\t7\t-8\t1\tM\t5\t250\t1\tT\tE\n0\t\t\t\t\t0\t-10\t\t\t0\t\t\t\t0\t\t\n");
    EXPECT_EQ(assemble_tsv(text, sized), bytes);
    for (std::size_t size = 0; size < bytes.size(); ++size)
        EXPECT_REJECTS(disassemble_tsv(std::span(bytes).first(size), remaining), "");
    auto negative_count = bytes;
    std::fill_n(negative_count.begin() + 4, 4, 255);
    EXPECT_REJECTS(disassemble_tsv(negative_count, remaining), "Array count");
}

TEST(Dat, ConditionalFields)
{
    const auto conditional = ([]
                              {
                                  auto schema = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"flags","type":"u32","count":2},{"name":"kind","type":"i32"},{"name":"n","type":"u32"},{"name":"pad","type":"filler","size":3},{"name":"value","type":"u32","enabledBy":[[{"field":"flags","value":2,"mask":2},{"field":"kind","value":3}],[{"field":"kind","value":-1}]]},{"name":"items","type":"unicode","count":"n","enabledBy":[[{"field":"flags","value":1,"mask":1}]]},{"name":"tail","type":"u32"}]}]})");
                                  schema.fields[5].soft = 1;
                                  return schema;
                              }());
    const std::vector<std::uint8_t> bytes = {
        4, 0, 0, 0,
        2, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0xa1, 0x86, 1, 0, 170, 170, 170, 77, 0, 0, 0, 9, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 88, 0, 0, 0, 10, 0, 0, 0,
        1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 255, 255, 255, 2, 0, 0, 0, 'A', 0, 2, 0, 0, 0, 'B', 0, 11, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 12, 0, 0, 0,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    Schema sized;
    const auto text = disassemble_tsv(bytes, conditional, &sized);
    EXPECT_EQ(text, "flags[0]\tflags[1]\tkind\tn\tpad{3}\tvalue\titems[0]\titems[1]\ttail\n2\t0\t3\t100001\t170\t77\t\t\t9\n0\t0\t-1\t0\t0\t88\t\t\t10\n1\t0\t0\t2\t255\t\tA\tB\t11\n0\t0\t3\t1\t1\t\t\t\t12\n");
    EXPECT_EQ(assemble_tsv(text, sized), bytes);
    auto mixed_filler = bytes;
    mixed_filler[21] = 171;
    EXPECT_REJECTS(disassemble_tsv(mixed_filler, conditional), "Nonuniform FILLER");
    EXPECT_ROUNDTRIP(tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"flag","type":"u32"},{"name":"optional","type":"u32","enabledBy":[[{"field":"flag","value":-1}]]}]}]})"), "4294967295\t5\n");
    EXPECT_ROUNDTRIP(tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"flag","type":"hex32"},{"name":"value","type":"u8","enabledBy":[[{"field":"flag","value":128,"mask":128}]]}]}]})"), "80\t7\n");
    EXPECT_ROUNDTRIP(([]
                      {
                          auto schema = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"flag","type":"u8"},{"name":"value","type":"mat","enabledBy":[[{"field":"flag","value":1}]]}]}]})");
                          schema.header = false;
                          schema.fields[1].softm = 1;
                          return schema;
                      }()),
                     "1\t0\t\t\n0\t\t\t\n");
    EXPECT_ROUNDTRIP(tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"flag","type":"u8"},{"name":"first","type":"u32","enabledBy":[[{"field":"flag","value":1}]]},{"name":"next","type":"u32","enabledBy":[[{"field":"first","value":0}]]}]}]})"), "1\t0\t7\n0\t\t9\n");
    EXPECT_REJECTS(assemble_tsv("0\t7\n", tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"flag","type":"u32"},{"name":"value","type":"u8","enabledBy":[[{"field":"flag","value":1}]]}]}]})")), "disabled field");
    EXPECT_REJECTS(assemble_tsv("256\n", tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"pad","type":"filler","size":3}]}]})")), "FILLER byte overflow");
}

TEST(Dat, Presentation)
{
    auto presentation = ([]
                         {
                             auto schema = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"a","type":"u32"},{"name":"hidden","type":"u32"},{"name":"vals","type":"i32","count":4},{"name":"f","type":"f32","count":3},{"name":"tied","type":"u32"},{"name":"z","type":"u32"}]}]})");
                             schema.fields[1].order = -1;
                             schema.fields[2].order = 2;
                             schema.fields[2].skip_if.push_back({0, 0, 3.0, 3.0});
                             schema.fields[2].skip_if.push_back({1, 2, -4.0, -2.0});
                             schema.fields[3].order = 1;
                             schema.fields[3].skip_if.push_back({0, 1, 1.25, 2.5});
                             schema.fields[4].order = 2;
                             return schema;
                         }());
    const std::vector<std::uint8_t> bytes = {
        1, 0, 0, 0, 10, 0, 0, 0, 99, 0, 0, 0,
        3, 0, 0, 0, 252, 255, 255, 255, 255, 255, 255, 255, 8, 0, 0, 0,
        0, 0, 160, 63, 0, 0, 32, 64, 0, 0, 96, 64, 20, 0, 0, 0, 30, 0, 0, 0,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    const auto presented = disassemble_tsv(bytes, presentation);
    EXPECT_EQ(presented, "f[0]\tf[1]\tf[2]\tvals[0]\tvals[1]\tvals[2]\tvals[3]\ttied\ta\tz\n\t\t3.5\t\t\t-1\t8\t20\t10\t30\n");
    EXPECT_EQ(disassemble_tsv(bytes, presentation), presented);
    EXPECT_REJECTS(assemble_tsv(presented, presentation), "columns");
    presentation.ignore_order = true;
    const auto ignored = disassemble_tsv(bytes, presentation);
    EXPECT_EQ(ignored, "a\thidden\tvals[0]\tvals[1]\tvals[2]\tvals[3]\tf[0]\tf[1]\tf[2]\ttied\tz\n10\t99\t\t\t-1\t8\t\t\t3.5\t20\t30\n");
    auto zeroed = bytes;
    for (const auto offset : {12, 16, 28, 32})
        std::fill_n(zeroed.begin() + offset, 4, 0);
    EXPECT_EQ(assemble_tsv(ignored, presentation), zeroed) << "Assembly must ignore ORD/SKIPIF and encode blank numerics as zero";
    auto ordered_compounds = compounds();
    ordered_compounds.fields[2].order = ordered_compounds.fields[3].order = 0;
    EXPECT_EQ(disassemble_tsv(compound_bytes, ordered_compounds), "1\tM\t0\t\t1\tU\t5\t16909060\t0\t\t1\t7\t\n") << "ORD split a compound or reordered ties";
    EXPECT_REJECTS(assemble_tsv("b\ta\n2\t1\n", ([]
                                                 {
                                                     auto schema = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"a","type":"u32"},{"name":"b","type":"u32"}]}]})");
                                                     schema.fields[1].order = 0;
                                                     return schema;
                                                 }())),
                   "header");
}

TEST(Dat, SkipIf)
{
    const std::vector<std::uint8_t> fraction_bytes = {1, 0, 0, 0, 205, 204, 204, 61, 205, 204, 76, 62, 205, 204, 204, 189, 12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    const auto fraction = ([]
                           {
                               auto schema = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"a","type":"f32","count":3}]}]})");
                               schema.header = false;
                               schema.fields[0].skip_if.push_back({0, 0, 0.10000000149011612, 0.10000000149011612});
                               schema.fields[0].skip_if.push_back({1, 1, 0.20000000298023224, 0.20000000298023224});
                               schema.fields[0].skip_if.push_back({2, 2, -0.10000000149011612, -0.10000000149011612});
                               return schema;
                           }());
    EXPECT_EQ(disassemble_tsv(fraction_bytes, fraction), "\t\t\n") << "SKIPIF must compare float32 values";
    const std::vector<std::uint8_t> unsigned_bytes = {1, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    EXPECT_EQ(disassemble_tsv(unsigned_bytes, ([]
                                               {
                                                   auto schema = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"a","type":"u32"},{"name":"b","type":"i32"},{"name":"c","type":"u16"}]}]})");
                                                   schema.header = false;
                                                   schema.fields[0].skip_if.push_back({0, 0, 0.0, 4294967295.0});
                                                   schema.fields[1].skip_if.push_back({0, 0, 4294967295.0, 4294967295.0});
                                                   schema.fields[2].skip_if.push_back({0, 0, -1.0, -1.0});
                                                   return schema;
                                               }())),
              "4294967295\t\t\n");
    const auto skip_nan = ([]
                           {
                               auto schema = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"a","type":"f32"}]}]})");
                               schema.header = false;
                               schema.fields[0].skip_if.push_back({0, 0, -3.4028234663852886e+38, 3.4028234663852886e+38});
                               return schema;
                           }());
    EXPECT_EQ(disassemble_tsv(assemble_tsv("NaN(0x7fc12345)\n", skip_nan), skip_nan), "NaN(0x7FC12345)\n");
    const auto skip_control = ([]
                               {
                                   auto schema = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"flag","type":"u32"},{"name":"value","type":"u32","enabledBy":[[{"field":"flag","value":1}]]}]}]})");
                                   schema.header = false;
                                   schema.fields[0].skip_if.push_back({0, 0, 1.0, 1.0});
                                   return schema;
                               }());
    EXPECT_EQ(disassemble_tsv(assemble_tsv("1\t7\n", skip_control), skip_control), "\t7\n") << "SKIPIF changed ENBBY input";
}

TEST(Dat, DynamicArrays)
{
    const auto dynamic = ([]
                          {
                              auto schema = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"n","type":"i32"},{"name":"a","type":"u32","count":"n"}]}]})");
                              schema.header = false;
                              schema.fields[1].soft = 1;
                              return schema;
                          }());
    EXPECT_REJECTS(assemble_tsv("-1\t\n", dynamic), "Array count");
    EXPECT_REJECTS(assemble_tsv("2\t7\n", dynamic), "SOFT");
    EXPECT_REJECTS(assemble_tsv("0\t7\n", dynamic), "padding");
}

TEST(Dat, CountersOut)
{
    const auto counts_off = ([]
                             {
                                 auto schema = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"mesh","type":"mtx"},{"name":"mats","type":"mat"},{"name":"special","type":"mat2"}]}]})");
                                 schema.header = false;
                                 schema.mtx_counts = false;
                                 schema.mat_counts = false;
                                 schema.fields[0].softm = 2;
                                 schema.fields[0].softt = 2;
                                 schema.fields[1].softm = 2;
                                 schema.fields[2].softm = 1;
                                 return schema;
                             }());
    const std::vector<std::uint8_t> bytes = {
        1, 0, 0, 0,
        1, 0, 0, 0, 2, 0, 0, 0, 'M', 0, 1, 0, 0, 0, 2, 0, 0, 0, 'T', 0,
        2, 0, 0, 0, 10, 0, 0, 0, 20, 0, 0, 0, 30, 0, 0, 0, 40, 0, 0, 0,
        1, 0, 0, 0, 99, 0, 0, 0, 50, 0, 0, 0, 60, 0, 0, 0,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    const std::string suppressed = "M\t\tT\t\t10\t20\t30\t40\t99\t50\t60\n";
    EXPECT_EQ(disassemble_tsv(bytes, counts_off), suppressed);
    EXPECT_EQ(disassemble_tsv(bytes, counts_off), suppressed);
    const auto counts_on = ([]
                            {
                                auto schema = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"mesh","type":"mtx"},{"name":"mats","type":"mat"},{"name":"special","type":"mat2"}]}]})");
                                schema.header = false;
                                schema.fields[0].softm = 2;
                                schema.fields[0].softt = 2;
                                schema.fields[1].softm = 2;
                                schema.fields[2].softm = 1;
                                return schema;
                            }());
    const auto text = disassemble_tsv(bytes, counts_on);
    EXPECT_EQ(text, "1\tM\t\t1\tT\t\t2\t10\t20\t30\t40\t1\t99\t50\t60\n");
    EXPECT_EQ(assemble_tsv(text, counts_off), bytes) << "Assembly must ignore CNT_OUT=NO and require counters";
    EXPECT_REJECTS(assemble_tsv(suppressed, counts_off), "columns");
}

TEST(Dat, FieldReferences)
{
    const std::vector<std::uint8_t> duplicate_bytes = {
        1, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 7, 0, 0, 0, 9, 0, 0, 0,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    const auto duplicates = ([]
                             {
                                 auto schema = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"n","type":"u32"},{"name":"n","type":"u32"},{"name":"b","type":"u32","count":"n"},{"name":"e","type":"u32","enabledBy":[[{"field":"n","value":1}]]}]}]})");
                                 schema.fields[2].soft = 1;
                                 return schema;
                             }());
    EXPECT_EQ(disassemble_tsv(duplicate_bytes, duplicates), "n\tn\tb\te\n1\t2\t7\t9\n");
    EXPECT_EQ(assemble_tsv(disassemble_tsv(duplicate_bytes, duplicates), duplicates), duplicate_bytes);
    const std::vector<std::uint8_t> array_counter_bytes = {
        1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 9, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    Schema array_counter = tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"c","type":"u32"},{"name":"e","type":"u32","count":"c"},{"name":"x","type":"u32","count":"e"}]}]})");
    EXPECT_EQ(disassemble_tsv(array_counter_bytes, array_counter, &array_counter), "2\t3\t9\t1\t2\t3\n");
    EXPECT_EQ(assemble_tsv("2\t3\t9\t1\t2\t3\n", array_counter), array_counter_bytes);
}

TEST(Dat, FieldSeparator)
{
    const auto comma = ([]
                        {
                            auto schema = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"id","type":"u32"},{"name":"name","type":"unicode"},{"name":"a","type":"u32"},{"name":"b","type":"u32"}]}]})");
                            schema.separator = ",";
                            return schema;
                        }());
    const std::vector<std::uint8_t> bytes = {
        1, 0, 0, 0, 7, 0, 0, 0, 10, 0, 0, 0, 'H', 0, 'e', 0, 'l', 0, 'l', 0, 'o', 0, 1, 0, 0, 0, 2, 0, 0, 0,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    EXPECT_EQ(disassemble_tsv(bytes, comma), "id,name,a,b\n7,Hello,1,2\n");
    EXPECT_EQ(comma.separator, ",");
    EXPECT_EQ(disassemble_tsv(bytes, ([]
                                      {
                                          auto schema = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"id","type":"u32"},{"name":"name","type":"unicode"},{"name":"a","type":"u32"},{"name":"b","type":"u32"}]}]})");
                                          schema.header = false;
                                          schema.separator = " | ";
                                          return schema;
                                      }())),
              "7 | Hello | 1 | 2\n");
    const auto tabbed = ([]
                         {
                             auto schema = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"id","type":"u32"},{"name":"name","type":"unicode"}]}]})");
                             schema.header = false;
                             schema.separator = "x\ty";
                             return schema;
                         }());
    EXPECT_EQ(tabbed.separator, "x\ty");
    EXPECT_EQ(tabbed.separator, "x\ty");
    EXPECT_EQ(assemble_tsv("id\tname\ta\tb\n7\tHello\t1\t2\n", comma), bytes) << "Assembly must ignore non-tab FS";
    EXPECT_REJECTS(assemble_tsv("id,name,a,b\n7,Hello,1,2\n", comma), "columns");
}

TEST(Dat, Groups)
{
    const auto groups = parse_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"id","type":"u32"},{"name":"n","type":"u8"},{"name":"drop","type":"group","count":"n","fields":[{"name":"mesh","type":"u32"},{"name":"t","type":"compact"},{"name":"tex","type":"u32","count":"t"}]},{"name":"tail","type":"u8","enabledBy":[[{"field":"n","value":0}]]}]}]})");
    const std::vector<std::uint8_t> bytes = {
        2, 0, 0, 0,
        7, 0, 0, 0, 2, 1, 0, 0, 0, 2, 5, 0, 0, 0, 6, 0, 0, 0, 9, 0, 0, 0, 0,
        8, 0, 0, 0, 0, 3,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    Schema sized;
    const auto text = disassemble_tsv(bytes, groups, &sized);
    EXPECT_EQ(text, "id\tn\tdrop[0].mesh\tdrop[0].t\tdrop[0].tex[0]\tdrop[0].tex[1]\tdrop[1].mesh\tdrop[1].t\tdrop[1].tex[0]\tdrop[1].tex[1]\ttail\n"
                    "7\t2\t1\t2\t5\t6\t9\t0\t\t\t\n"
                    "8\t0\t\t\t\t\t\t\t\t\t3\n");
    EXPECT_EQ(sized.fields[2].soft, 2u);
    EXPECT_EQ(sized.fields[2].members[2].soft, 2u);
    EXPECT_EQ(assemble_tsv(text, sized), bytes);
    EXPECT_REJECTS(assemble_tsv("id\tn\tdrop[0].mesh\tdrop[0].t\tdrop[0].tex[0]\tdrop[0].tex[1]\tdrop[1].mesh\tdrop[1].t\tdrop[1].tex[0]\tdrop[1].tex[1]\ttail\n"
                                "8\t0\t1\t\t\t\t\t\t\t\t3\n",
                                sized),
                   "Nonempty padding or disabled field");

    const auto nested = tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"k","type":"u8"},{"name":"g","type":"group","count":2,"fields":[{"name":"k","type":"u8"},{"name":"v","type":"u8","count":"k"},{"name":"o","type":"u8","enabledBy":[[{"field":"k","value":1}]]}]},{"name":"s","type":"group","fields":[{"name":"a","type":"u8"}]}]}]})");
    const std::vector<std::uint8_t> nested_bytes = {1, 0, 0, 0, 3, 1, 4, 5, 2, 6, 7, 9, 12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    EXPECT_EQ(disassemble_tsv(nested_bytes, nested, &sized), "3\t1\t4\t\t5\t2\t6\t7\t\t9\n");
    EXPECT_EQ(assemble_tsv("3\t1\t4\t\t5\t2\t6\t7\t\t9\n", sized), nested_bytes);
    const auto outer = tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"n","type":"u8"},{"name":"f","type":"u8"},{"name":"g","type":"group","count":"n","fields":[{"name":"c","type":"u8"},{"name":"h","type":"group","count":"n","fields":[{"name":"x","type":"u8","enabledBy":[[{"field":"f","value":1}]]}]}]}]}]})");
    const std::vector<std::uint8_t> outer_bytes = {2, 0, 0, 0, 2, 1, 1, 2, 3, 4, 5, 6, 1, 0, 7, 12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    const std::string outer_text = "2\t1\t1\t2\t3\t4\t5\t6\n1\t0\t7\t\t\t\t\t\n";
    EXPECT_EQ(disassemble_tsv(outer_bytes, outer, &sized), outer_text);
    EXPECT_EQ(assemble_tsv(outer_text, sized), outer_bytes);
}

TEST(Dat, SixtyFourBit)
{
    const auto wide = tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"a","type":"i64"},{"name":"b","type":"f64"},{"name":"n","type":"u32"},{"name":"c","type":"i64","count":"n"}]}]})");
    const std::vector<std::uint8_t> bytes = {
        1, 0, 0, 0,
        0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0xf8, 0x3f, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    Schema sized;
    EXPECT_EQ(disassemble_tsv(bytes, wide, &sized), "-2\t1.5\t1\t-9223372036854775808\n");
    EXPECT_EQ(assemble_tsv("-2\t1.5\t1\t-9223372036854775808\n", sized), bytes);
    EXPECT_ROUNDTRIP(tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"a","type":"i64"},{"name":"b","type":"f64"}]}]})"), "9223372036854775807\tNaN(0x7FF8000000000001)\n");
    EXPECT_ROUNDTRIP(tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"b","type":"f64"}]}]})"), "-0\n4.9406564584124654e-324\ninf\n");
    EXPECT_REJECTS(assemble_tsv("9223372036854775808\t0\n", tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"a","type":"i64"},{"name":"b","type":"f64"}]}]})")), "integer");
    EXPECT_REJECTS(assemble_tsv("0\tNaN(0x1)\n", tsv_schema(R"({"version":1,"trailer":true,"magic":0,"blocks":[{"recordCount":"u32","fields":[{"name":"a","type":"i64"},{"name":"b","type":"f64"}]}]})")), "NaN");
}
