#include <l2asmdisasm.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace l2asmdisasm;

void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void rejects(Function function, std::string_view expected)
{
    try
    {
        function();
    }
    catch (const std::invalid_argument &error)
    {
        require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());
        return;
    }
    throw std::runtime_error("Invalid input accepted");
}

void roundtrip(const Schema &schema, std::string_view text)
{
    const auto bytes = assemble(text, schema);
    const auto decoded = disassemble(bytes, schema);
    require(assemble(decoded, schema) == bytes, "Binary round trip failed");
}
} // namespace

int main()
{
    try
    {
        const auto schema = parse_ddf(R"(
            # comment
            FS = "\t"; HEADER = YES; RECCNT = OFF;
            MTXCNT_OUT = YES; MATCNT_OUT = YES; ORD_IGNORE = NO;
            /* a block comment */
            { UINT id; UNICODE name; ASCF description; CHEX flags[2]; } // comment
        )");
        const std::string text = "id\tname\tdescription\tflags[0]\tflags[1]\n17\tAΩ\ta,x\\0\\t\\\\\tFF\t0\n";
        const std::vector<std::uint8_t> expected = {
            1, 0, 0, 0, 17, 0, 0, 0, 4, 0, 0, 0, 0x41, 0, 0xa9, 3,
            4, 0x78, 0, 9, 0x5c, 0xff, 0,
            12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
        require(assemble(text, schema) == expected, "Known DAT encoding mismatch");
        require(disassemble(expected, schema) == text, "Known DAT decoding mismatch");

        const auto strings = parse_ddf("HEADER=NO; { UNICODE name; ASCF value; }");
        roundtrip(strings, "汉字😀\\0\tu,A😀\\0\n");
        roundtrip(strings, "\ta,\n\ta,\\0\n\ta,éÿ\\n\\r\\t\\\\\n\ta,unterminated\n");
        const auto wide = assemble("\tu,A😀\n", strings);
        require(wide[8] == 0x83 && wide[9] == 0x41 && wide[11] == 0x3d && wide[12] == 0xd8, "ASCF length is not UTF-16 code units");
        rejects([&]
                { (void)assemble("\ta,汉\n", strings); }, "ISO-8859-1");
        rejects([&]
                { (void)assemble("\tu,\n", strings); }, "Empty wide ASCF");
        rejects([&]
                { (void)assemble("\ta,\\z\n", strings); }, "Unknown text escape");
        rejects([&]
                { (void)assemble("\ta,\\\n", strings); }, "Unterminated text escape");
        rejects([&]
                { (void)assemble(std::string("\ta,") + char(0xff) + "\n", strings); }, "Invalid UTF-8");

        const auto numbers = parse_ddf("HEADER=NO; { UINT a; INT b; UWORD c; WORD d; UCHAR e; CHAR f; HEX g; CHEX h; CNTR i; FLOAT j; }");
        roundtrip(numbers, "4294967295\t-2147483648\t65535\t-32768\t255\t-128\t0xdeadbeef\t0xff\t-2147483648\tNaN(0xffa12345)\n");
        roundtrip(numbers, "0\t2147483647\t0\t32767\t0\t127\t0x0\t0x0\t2147483647\t-0\n");
        for (const auto value : {"1.40129846e-45", "3.40282347e+38", "inf", "-inf", "1.25"})
            roundtrip(parse_ddf("HEADER=NO; { FLOAT value; }"), std::string(value) + "\n");
        const auto narrow = parse_ddf("HEADER=NO; { UCHAR a; WORD b; }");
        rejects([&]
                { (void)assemble("256\t0\n", narrow); }, "overflow");
        rejects([&]
                { (void)assemble("0\t-32769\n", narrow); }, "overflow");
        rejects([&]
                { (void)assemble("-1\t0\n", narrow); }, "integer");
        rejects([&]
                { (void)assemble("0\n", narrow); }, "columns");

        const auto flagged = parse_ddf("HEADER=NO; RECCNT=on; { UCHAR id; UINT on; }");
        const auto flagged_bytes = assemble("1\t1\n2\t0\n3\t5\n", flagged);
        require(flagged_bytes[0] == 2 && flagged_bytes.size() == 4 + 15 + 13, "RECCNT field count mismatch");
        require(disassemble(flagged_bytes, flagged) == "1\t1\n2\t0\n3\t5\n", "RECCNT field decoding mismatch");
        require(export_ddf(flagged).find("RECCNT = on;") != std::string::npos, "RECCNT field export mismatch");
        auto miscounted = flagged_bytes;
        miscounted[0] = 3;
        rejects([&]
                { (void)disassemble(miscounted, flagged); }, "differs from the 2 records");
        rejects([]
                { (void)parse_ddf("RECCNT=missing; { UINT a; }"); }, "RECCNT must name an integer field");

        const auto fixed = parse_ddf("HEADER=NO; RECCNT=1; MAGIC=2; { UCHAR v; }");
        const auto fixed_bytes = assemble("42\n", fixed);
        require(fixed_bytes.size() == 22 && fixed_bytes[0] == 42 && fixed_bytes[9] == 12, "RECCNT/MAGIC wire layout mismatch");
        require(disassemble(fixed_bytes, fixed) == "42\n", "RECCNT/MAGIC decoding mismatch");
        rejects([&]
                { (void)assemble("", fixed); }, "RECCNT");
        auto bad_magic = fixed_bytes;
        bad_magic[1] = 1;
        rejects([&]
                { (void)disassemble(bad_magic, fixed); }, "MAGIC");

        for (std::size_t size = 0; size < expected.size(); ++size)
            rejects([&]
                    { (void)disassemble(std::span(expected).first(size), schema); }, "");
        auto malformed = expected;
        malformed.push_back(0);
        rejects([&]
                { (void)disassemble(malformed, schema); }, "trailing bytes");
        malformed = expected;
        malformed[0] = 0xff;
        rejects([&]
                { (void)disassemble(malformed, schema); }, "Record count");
        malformed = expected;
        malformed[8] = 0xff;
        rejects([&]
                { (void)disassemble(malformed, schema); }, "Record 1, field name, byte 8");
        malformed = expected;
        malformed[12] = 0;
        malformed[13] = 0xd8;
        rejects([&]
                { (void)disassemble(malformed, schema); }, "surrogate");
        rejects([&]
                { (void)assemble("wrong\theader\n", schema); }, "columns");
        rejects([&]
                { (void)assemble("id\tname\tdescription\tflags[1]\tflags[0]\n", schema); }, "header");
        rejects([&]
                { (void)assemble("", schema); }, "Missing text header");
        require(disassemble(assemble("id\tname\tdescription\tflags[0]\tflags[1]\n", schema), schema) == "id\tname\tdescription\tflags[0]\tflags[1]\n", "Empty table mismatch");

        const auto compounds = parse_ddf("HEADER=NO; { UINT n; UCHAR a[n]; SOFT=2; MTX mesh; SOFTM=1; SOFTT=1; MTX2 extra; SOFTM=1; SOFTT=1; }");
        const std::string compound_text = "1\t7\t\t1\tM\t0\t\t1\tU\t5\t16909060\t0\t\n";
        const std::vector<std::uint8_t> compound_bytes = {
            1, 0, 0, 0, 1, 0, 0, 0, 7,
            1, 0, 0, 0, 2, 0, 0, 0, 'M', 0, 0, 0, 0, 0,
            1, 0, 0, 0, 2, 0, 0, 0, 'U', 0, 5, 4, 3, 2, 1, 0, 0, 0, 0,
            12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
        require(assemble(compound_text, compounds) == compound_bytes, "Compound wire vector mismatch");
        require(disassemble(compound_bytes, compounds) == compound_text, "Compound text vector mismatch");
        Schema sized;
        const auto inferred = disassemble(compound_bytes, parse_ddf("HEADER=NO; { UINT n; UCHAR a[n]; MTX mesh; MTX2 extra; }"), &sized);
        require(sized.fields[1].soft == 1 && sized.fields[3].softm == 1 && sized.fields[3].softt == 1, "Inferred SOFT widths mismatch");
        require(assemble(inferred, parse_ddf(export_ddf(sized))) == compound_bytes, "Exported DDF round trip failed");
        const auto remaining = parse_ddf("HEADER=NO; { MAT materials; MAT2 special; MTX3 mesh; }");
        const std::vector<std::uint8_t> remaining_bytes = {
            2, 0, 0, 0,
            2, 0, 0, 0, 255, 255, 255, 255, 254, 255, 255, 255, 0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55,
            1, 0, 0, 0, 247, 255, 255, 255, 7, 0, 0, 0, 248, 255, 255, 255,
            1, 0, 0, 0, 2, 0, 0, 0, 'M', 0, 5, 250, 1, 0, 0, 0, 2, 0, 0, 0, 'T', 0, 2, 0, 0, 0, 'E', 0,
            0, 0, 0, 0, 0, 0, 0, 0, 246, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
        const auto remaining_text = disassemble(remaining_bytes, remaining, &sized);
        require(remaining_text == "2\t-1\t-2\t287454020\t1432778632\t1\t-9\t7\t-8\t1\tM\t5\t250\t1\tT\tE\n0\t\t\t\t\t0\t-10\t\t\t0\t\t\t\t0\t\t\n", "MAT/MTX3 text mismatch");
        require(assemble(remaining_text, parse_ddf(export_ddf(sized))) == remaining_bytes, "MAT/MTX3 wire mismatch");
        for (std::size_t size = 0; size < remaining_bytes.size(); ++size)
            rejects([&]
                    { (void)disassemble(std::span(remaining_bytes).first(size), remaining); }, "");
        auto negative_count = remaining_bytes;
        std::fill_n(negative_count.begin() + 4, 4, 255);
        rejects([&]
                { (void)disassemble(negative_count, remaining); }, "Array count");
        const auto conditional = parse_ddf(R"({
            UINT flags[2]; INT kind; UINT n; FILLER pad{3};
            UINT value; ENBBY=[(flags:2,2),(kind,3)]; ENBBY=[(kind,-1)];
            UNICODE items[n]; SOFT=1; ENBBY=[(flags:1,1)]; UINT tail;
        })");
        const std::vector<std::uint8_t> conditional_bytes = {
            4, 0, 0, 0,
            2, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0xa1, 0x86, 1, 0, 170, 170, 170, 77, 0, 0, 0, 9, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 88, 0, 0, 0, 10, 0, 0, 0,
            1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 255, 255, 255, 2, 0, 0, 0, 'A', 0, 2, 0, 0, 0, 'B', 0, 11, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 12, 0, 0, 0,
            12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
        const auto conditional_text = disassemble(conditional_bytes, conditional, &sized);
        require(conditional_text == "flags[0]\tflags[1]\tkind\tn\tpad{3}\tvalue\titems[0]\titems[1]\ttail\n2\t0\t3\t100001\t170\t77\t\t\t9\n0\t0\t-1\t0\t0\t88\t\t\t10\n1\t0\t0\t2\t255\t\tA\tB\t11\n0\t0\t3\t1\t1\t\t\t\t12\n", "FILLER/ENBBY legacy text mismatch");
        require(assemble(conditional_text, parse_ddf(export_ddf(sized))) == conditional_bytes, "FILLER/ENBBY wire mismatch");
        auto mixed_filler = conditional_bytes;
        mixed_filler[21] = 171;
        rejects([&]
                { (void)disassemble(mixed_filler, conditional); }, "Nonuniform FILLER");
        roundtrip(parse_ddf("HEADER=NO; { UINT flag; UINT optional; ENBBY=[(flag,-1)]; }"), "4294967295\t5\n");
        roundtrip(parse_ddf("HEADER=NO; { HEX flag; UCHAR value; ENBBY=[(flag:128,128)]; }"), "80\t7\n");
        roundtrip(parse_ddf("HEADER=NO; { UCHAR flag; MAT value; SOFTM=1; ENBBY=[(flag,1)]; }"), "1\t0\t\t\n0\t\t\t\n");
        roundtrip(parse_ddf("HEADER=NO; { UCHAR flag; UINT first; ENBBY=[(flag,1)]; UINT next; ENBBY=[(first,0)]; }"), "1\t0\t7\n0\t\t9\n");
        rejects([&]
                { (void)assemble("0\t7\n", parse_ddf("HEADER=NO; { UINT flag; UCHAR value; ENBBY=[(flag,1)]; }")); }, "disabled field");
        rejects([&]
                { (void)assemble("256\n", parse_ddf("HEADER=NO; { FILLER pad{3}; }")); }, "FILLER byte overflow");
        for (const auto ddf : {"{ FILLER x{0}; }", "{ FILLER x{3}[2]; }", "{ UINT x; ENBBY=[]; }", "{ UINT x; ENBBY=[(later,1)]; UINT later; }", "{ ASCF s; UINT x; ENBBY=[(s,1)]; }", "{ UINT n; UINT x; ENBBY=[(n:4294967296,1)]; }"})
            rejects([&]
                    { (void)parse_ddf(ddf); }, "DDF ");
        auto presentation = parse_ddf("{ UINT a; UINT hidden; ORD=OFF; INT vals[4]; SKIPIF=[(0,3),(1 .. 2,-4 .. -2)]; ORD=2; FLOAT f[3]; SKIPIF=[(0 .. 1,1.25 .. 2.5)]; ORD=1; UINT tied; ORD=2; UINT z; }");
        const std::vector<std::uint8_t> presentation_bytes = {
            1, 0, 0, 0, 10, 0, 0, 0, 99, 0, 0, 0,
            3, 0, 0, 0, 252, 255, 255, 255, 255, 255, 255, 255, 8, 0, 0, 0,
            0, 0, 160, 63, 0, 0, 32, 64, 0, 0, 96, 64, 20, 0, 0, 0, 30, 0, 0, 0,
            12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
        const auto presented = disassemble(presentation_bytes, presentation);
        require(presented == "f[0]\tf[1]\tf[2]\tvals[0]\tvals[1]\tvals[2]\tvals[3]\ttied\ta\tz\n\t\t3.5\t\t\t-1\t8\t20\t10\t30\n", "ORD/SKIPIF presentation mismatch");
        require(disassemble(presentation_bytes, parse_ddf(export_ddf(presentation))) == presented, "Presentation properties lost in DDF export");
        rejects([&]
                { (void)assemble(presented, presentation); }, "columns");
        presentation.ignore_order = true;
        const auto ignored = disassemble(presentation_bytes, presentation);
        require(ignored == "a\thidden\tvals[0]\tvals[1]\tvals[2]\tvals[3]\tf[0]\tf[1]\tf[2]\ttied\tz\n10\t99\t\t\t-1\t8\t\t\t3.5\t20\t30\n", "ORD_IGNORE mismatch");
        auto zeroed = presentation_bytes;
        for (const auto offset : {12, 16, 28, 32})
            std::fill_n(zeroed.begin() + offset, 4, 0);
        require(assemble(ignored, presentation) == zeroed, "Assembly must ignore ORD/SKIPIF and encode blank numerics as zero");
        auto ordered_compounds = compounds;
        ordered_compounds.fields[2].order = ordered_compounds.fields[3].order = 0;
        require(disassemble(compound_bytes, ordered_compounds) == "1\tM\t0\t\t1\tU\t5\t16909060\t0\t\t1\t7\t\n", "ORD split a compound or reordered ties");
        const std::vector<std::uint8_t> fraction_bytes = {1, 0, 0, 0, 205, 204, 204, 61, 205, 204, 76, 62, 205, 204, 204, 189, 12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
        const auto fraction = parse_ddf("HEADER=NO; { FLOAT a[3]; SKIPIF=[(0,0.1),(1,0.2),(2,-0.1)]; }");
        require(disassemble(fraction_bytes, parse_ddf(export_ddf(fraction))) == "\t\t\n", "SKIPIF must compare float32 values");
        const std::vector<std::uint8_t> unsigned_bytes = {1, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
        require(disassemble(unsigned_bytes, parse_ddf("HEADER=NO; { UINT a; SKIPIF=[(0,0 .. 4294967295)]; INT b; SKIPIF=[(0,4294967295)]; UWORD c; SKIPIF=[(0,-1)]; }")) == "4294967295\t\t\n", "SKIPIF integer sign/range mismatch");
        const auto skip_nan = parse_ddf("HEADER=NO; { FLOAT a; SKIPIF=[(0,-3.40282347e38 .. 3.40282347e38)]; }");
        require(disassemble(assemble("NaN(0x7fc12345)\n", skip_nan), skip_nan) == "NaN(0x7FC12345)\n", "SKIPIF suppressed a NaN");
        const auto skip_control = parse_ddf("HEADER=NO; { UINT flag; SKIPIF=[(0,1)]; UINT value; ENBBY=[(flag,1)]; }");
        require(disassemble(assemble("1\t7\n", skip_control), skip_control) == "\t7\n", "SKIPIF changed ENBBY input");
        rejects([&]
                { (void)assemble("b\ta\n2\t1\n", parse_ddf("{ UINT a; UINT b; ORD=0; }")); }, "header");
        for (const auto ddf : {"{ UINT a; ORD=1; ORD=2; }", "{ UINT a; ORD=2147483648; }", "{ ASCF s; SKIPIF=[(0,0)]; }", "{ CNTR n; SKIPIF=[(0,0)]; }", "{ FILLER f{2}; SKIPIF=[(0,0)]; }", "{ UINT a; SKIPIF=[]; }", "{ UINT a; SKIPIF=[(-1,0)]; }", "{ UINT a; SKIPIF=[(2 .. 1,0)]; }", "{ UINT a; SKIPIF=[(0,3 .. 2)]; }", "{ UINT a; SKIPIF=[(0,0.5)]; }", "{ FLOAT a; SKIPIF=[(0,nan)]; }"})
            rejects([&]
                    { (void)parse_ddf(ddf); }, "DDF ");
        const auto dynamic = parse_ddf("HEADER=NO; { INT n; UINT a[n]; SOFT=1; }");
        rejects([&]
                { (void)assemble("-1\t\n", dynamic); }, "Array count");
        rejects([&]
                { (void)assemble("2\t7\n", dynamic); }, "SOFT");
        rejects([&]
                { (void)assemble("0\t7\n", dynamic); }, "padding");
        const auto counts_off = parse_ddf("HEADER=NO; MTXCNT_OUT=NO; MATCNT_OUT=NO; { MTX mesh; SOFTM=2; SOFTT=2; MAT mats; SOFTM=2; MAT2 special; SOFTM=1; }");
        const std::vector<std::uint8_t> counts_bytes = {
            1, 0, 0, 0,
            1, 0, 0, 0, 2, 0, 0, 0, 'M', 0, 1, 0, 0, 0, 2, 0, 0, 0, 'T', 0,
            2, 0, 0, 0, 10, 0, 0, 0, 20, 0, 0, 0, 30, 0, 0, 0, 40, 0, 0, 0,
            1, 0, 0, 0, 99, 0, 0, 0, 50, 0, 0, 0, 60, 0, 0, 0,
            12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
        require(disassemble(counts_bytes, counts_off) == "M\t\tT\t\t10\t20\t30\t40\t99\t50\t60\n", "Suppressed compound counters mismatch");
        require(disassemble(counts_bytes, parse_ddf(export_ddf(counts_off))) == "M\t\tT\t\t10\t20\t30\t40\t99\t50\t60\n", "CNT_OUT lost in DDF export");
        const auto counts_on = parse_ddf("HEADER=NO; { MTX mesh; SOFTM=2; SOFTT=2; MAT mats; SOFTM=2; MAT2 special; SOFTM=1; }");
        const auto counts_text = disassemble(counts_bytes, counts_on);
        require(counts_text == "1\tM\t\t1\tT\t\t2\t10\t20\t30\t40\t1\t99\t50\t60\n", "Full compound counters mismatch");
        require(assemble(counts_text, counts_off) == counts_bytes, "Assembly must ignore CNT_OUT=NO and require counters");
        rejects([&]
                { (void)assemble("M\t\tT\t\t10\t20\t30\t40\t99\t50\t60\n", counts_off); }, "columns");
        for (const auto ddf : {"{ FLOAT n; UINT x[n]; }", "{ MAT n; UINT x[n]; }", "{ MAT x; SOFTT=1; }", "{ UINT x; SOFT=1; }", "{ MTX x; SOFTM=100001; }"})
            rejects([&]
                    { (void)parse_ddf(ddf); }, "DDF ");

        const std::vector<std::uint8_t> duplicate_bytes = {
            1, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 7, 0, 0, 0, 9, 0, 0, 0,
            12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
        const auto duplicates = parse_ddf("{ UINT n; UINT n; UINT b[n]; SOFT=1; UINT e; ENBBY=[(n,1)]; }");
        require(disassemble(duplicate_bytes, duplicates) == "n\tn\tb\te\n1\t2\t7\t9\n", "Duplicate field references mismatch");
        require(assemble(disassemble(duplicate_bytes, duplicates), duplicates) == duplicate_bytes, "Duplicate field round trip failed");
        const std::vector<std::uint8_t> array_counter_bytes = {
            1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 9, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0,
            12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
        Schema array_counter = parse_ddf("HEADER=NO; { UINT c; UINT e[c]; UINT x[e]; }");
        require(disassemble(array_counter_bytes, array_counter, &array_counter) == "2\t3\t9\t1\t2\t3\n", "Array counter mismatch");
        require(assemble("2\t3\t9\t1\t2\t3\n", parse_ddf(export_ddf(array_counter))) == array_counter_bytes, "Array counter export lost");
        const auto repeated = parse_ddf("FS=\"|\"; HEADER=1; FS=\",\"; HEADER=0; { UINT a; }");
        require(repeated.separator == "," && !repeated.header, "Repeated global did not override");

        const auto comma = parse_ddf("FS=\",\"; HEADER=YES; { UINT id; UNICODE name; UINT a; UINT b; }");
        const std::vector<std::uint8_t> comma_bytes = {
            1, 0, 0, 0, 7, 0, 0, 0, 10, 0, 0, 0, 'H', 0, 'e', 0, 'l', 0, 'l', 0, 'o', 0, 1, 0, 0, 0, 2, 0, 0, 0,
            12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
        require(disassemble(comma_bytes, comma) == "id,name,a,b\n7,Hello,1,2\n", "Comma FS mismatch");
        require(parse_ddf(export_ddf(comma)).separator == ",", "FS lost in DDF export");
        require(disassemble(comma_bytes, parse_ddf("FS=\" | \"; HEADER=NO; { UINT id; UNICODE name; UINT a; UINT b; }")) ==
                    "7 | Hello | 1 | 2\n",
                "Multi-character FS mismatch");
        const auto tabbed = parse_ddf("FS=\"x\\ty\"; HEADER=NO; { UINT id; UNICODE name; }");
        require(tabbed.separator == "x\ty", "FS \\t escape mismatch");
        require(parse_ddf(export_ddf(tabbed)).separator == "x\ty", "FS with tab lost in export");
        require(assemble("id\tname\ta\tb\n7\tHello\t1\t2\n", comma) == comma_bytes, "Assembly must ignore non-tab FS");
        rejects([&]
                { (void)assemble("id,name,a,b\n7,Hello,1,2\n", comma); }, "columns");
        rejects([&]
                { (void)parse_ddf("FS=\"\"; { UINT a; }"); }, "FS");
        for (const auto ddf : {"{}", "{ UNKNOWN mesh; }", "{ UINT x[n]; }", "{ UINT x[0]; }", "{ UINT x; ORDER=1; }", "{ UINT x; } trailing", "/* unterminated", "FS=\"unterminated", "HEADER=3; {UINT a;}", "RECCNT=-2; {UINT a;}", "MAGIC=-1; {UINT a;}", "BOGUS=1; {UINT a;}"})
            rejects([&]
                    { (void)parse_ddf(ddf); }, "DDF ");

        {
            const auto both = parse_ddf("HEADER=NO; { UNICODE u; ASCF w; ASCF n; }");
            const std::vector<std::uint8_t> dat = {
                1, 0, 0, 0,
                12, 0, 0, 0, 0x41, 0x01, 0x1c, 0x20, 0xe9, 0, 0xa0, 0, 0x26, 0x20, 0x22, 0x6f,
                0x82, 0x10, 0x04, 0x1d, 0x20,
                0x03, 0xc0, 0x98 - 0x18, 0xff,
                12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
            TextOptions legacy;
            legacy.legacy = true;
            require(disassemble(dat, both, nullptr, legacy) == "-\"\xe9 .-\tu,-\"\ta,\xc0\x80\xff\n", "Legacy text decoding mismatch");
            const auto legacy_bytes = assemble("\xe9-\tu,\xff\ta,\xc0\x80\n", both, legacy);
            const std::vector<std::uint8_t> legacy_expected = {
                1, 0, 0, 0, 4, 0, 0, 0, 0xe9, 0, '-', 0, 0x81, 0xff, 0, 0x02, 0xc0, 0x80,
                12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
            require(legacy_bytes == legacy_expected, "Legacy text encoding mismatch");

            TextOptions forced;
            forced.force_ascii = true;
            require(disassemble(dat, both).find("\tu,А”\ta,À\u0080ÿ\n") != std::string::npos, "Default text decoding mismatch");
            require(disassemble(dat, both, nullptr, forced).find("\ta,А”\ta,") != std::string::npos, "-f did not force the a, hint");
            const auto ascii = assemble("\tu,é\ta,\n", both, forced);
            require(ascii[8] == 1 && ascii[9] == 0xe9, "-f did not force 8-bit ASCF encoding");
            rejects([&]
                    { (void)assemble("\tu,Д\ta,\n", both, forced); }, "ISO-8859-1");

            TextOptions cyrillic;
            cyrillic.charset = "windows-1251";
            require(disassemble(dat, both, nullptr, cyrillic).ends_with("\ta,АЂя\n"), "CP1251 decoding mismatch");
            const auto cp1251 = assemble("\ta,\ta,Ая€\n", both, cyrillic);
            require(cp1251[9] == 3 && cp1251[10] == 0xc0 && cp1251[11] == 0xff && cp1251[12] == 0x88, "CP1251 encoding mismatch");
            rejects([&]
                    { (void)assemble("\ta,\ta,é\n", both, cyrillic); }, "CP1251");
            const std::vector<std::uint8_t> undefined = {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0x98, 12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
            rejects([&]
                    { (void)disassemble(undefined, both, nullptr, cyrillic); }, "undefined in CP1251");
            TextOptions unknown;
            unknown.charset = "EBCDIC";
            rejects([&]
                    { (void)assemble("", both, unknown); }, "Unsupported charset");
            require(supported_charsets().front() == "ISO-8859-1", "Missing default charset");

            const auto floats = parse_ddf("HEADER=NO; { FLOAT f; SKIPIF = [(0, 2.5)]; }");
            const auto bits = assemble("1.40129846e-45\n0.1\n-0\n2.5\nNaN(0x7FC00001)\n3.40282347e+38\ninf\n", floats);
            TextOptions fixed, scientific;
            fixed.float_format = FloatFormat::Fixed;
            scientific.float_format = FloatFormat::Scientific;
            require(disassemble(bits, floats, nullptr, fixed) ==
                        "0.00000000\n0.10000000\n-0.00000000\n\nNaN(0x7FC00001)\n340282346638528859811704183484516925440.00000000\ninf\n",
                    "Fixed FLOAT output mismatch");
            const auto exponent = disassemble(bits, floats, nullptr, scientific);
            require(exponent == "1.40129846e-045\n1.00000001e-001\n-0.00000000e+000\n\nNaN(0x7FC00001)\n3.40282347e+038\ninf\n",
                    "Scientific FLOAT output mismatch");
            require(assemble(exponent, floats) == assemble(disassemble(bits, floats), floats), "Scientific FLOAT is not lossless");
        }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
