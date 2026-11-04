#include "support.h"

using namespace l2asmdisasm;

namespace
{
const Schema &both()
{
    static const auto schema = tsv_schema(R"({"blocks":[{"fields":[{"name":"u","type":"utf16"},{"name":"w","type":"string"},{"name":"n","type":"string"}]}]})");
    return schema;
}

const std::vector<std::uint8_t> dat = {
    1, 0, 0, 0,
    12, 0, 0, 0, 0x41, 0x01, 0x1c, 0x20, 0xe9, 0, 0xa0, 0, 0x26, 0x20, 0x22, 0x6f,
    0x82, 0x10, 0x04, 0x1d, 0x20,
    0x03, 0xc0, 0x98 - 0x18, 0xff,
    12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
} // namespace

TEST(Text, Legacy)
{
    TextOptions legacy;
    legacy.legacy = true;
    EXPECT_EQ(disassemble_tsv(dat, both(), nullptr, legacy), "-\"\xe9 .-\tu,-\"\ta,\xc0\x80\xff\n");
    const std::vector<std::uint8_t> expected = {
        1, 0, 0, 0, 4, 0, 0, 0, 0xe9, 0, '-', 0, 0x81, 0xff, 0, 0x02, 0xc0, 0x80,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    EXPECT_EQ(assemble_tsv("\xe9-\tu,\xff\ta,\xc0\x80\n", both(), legacy), expected);
}

TEST(Text, ForceAscii)
{
    TextOptions forced;
    forced.force_ascii = true;
    EXPECT_NE(disassemble_tsv(dat, both()).find("\tu,А”\ta,À\u0080ÿ\n"), std::string::npos);
    EXPECT_NE(disassemble_tsv(dat, both(), nullptr, forced).find("\ta,А”\ta,"), std::string::npos);
    const auto ascii = assemble_tsv("\tu,é\ta,\n", both(), forced);
    EXPECT_EQ(ascii[8], 1);
    EXPECT_EQ(ascii[9], 0xe9);
    EXPECT_REJECTS(assemble_tsv("\tu,Д\ta,\n", both(), forced), "ISO-8859-1");
}

TEST(Text, Charset)
{
    TextOptions cyrillic;
    cyrillic.charset = "windows-1251";
    EXPECT_TRUE(disassemble_tsv(dat, both(), nullptr, cyrillic).ends_with("\ta,АЂя\n"));
    const auto cp1251 = assemble_tsv("\ta,\ta,Ая€\n", both(), cyrillic);
    EXPECT_TRUE(cp1251[9] == 3 && cp1251[10] == 0xc0 && cp1251[11] == 0xff && cp1251[12] == 0x88);
    EXPECT_REJECTS(assemble_tsv("\ta,\ta,é\n", both(), cyrillic), "CP1251");
    const std::vector<std::uint8_t> undefined = {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0x98, 12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    EXPECT_REJECTS(disassemble_tsv(undefined, both(), nullptr, cyrillic), "undefined in CP1251");
    TextOptions unknown;
    unknown.charset = "EBCDIC";
    EXPECT_REJECTS(assemble_tsv("", both(), unknown), "Unsupported charset");
    EXPECT_EQ(supported_charsets().front(), "ISO-8859-1");
}

TEST(Text, FloatFormat)
{
    const auto floats = ([]
                         {
                             auto schema = parse_schema(R"({"blocks":[{"fields":[{"name":"f","type":"f32"}]}]})");
                             schema.header = false;
                             schema.fields[0].skip_if.push_back({0, 0, 2.5, 2.5});
                             return schema; }());
    const auto bits = assemble_tsv("1.40129846e-45\n0.1\n-0\n2.5\nNaN(0x7FC00001)\n3.40282347e+38\ninf\n", floats);
    TextOptions fixed_point, scientific;
    fixed_point.float_format = FloatFormat::Fixed;
    scientific.float_format = FloatFormat::Scientific;
    EXPECT_EQ(disassemble_tsv(bits, floats, nullptr, fixed_point),
              "0.00000000\n0.10000000\n-0.00000000\n\nNaN(0x7FC00001)\n340282346638528859811704183484516925440.00000000\ninf\n");
    const auto exponent = disassemble_tsv(bits, floats, nullptr, scientific);
    EXPECT_EQ(exponent, "1.40129846e-045\n1.00000001e-001\n-0.00000000e+000\n\nNaN(0x7FC00001)\n3.40282347e+038\ninf\n");
    EXPECT_EQ(assemble_tsv(exponent, floats), assemble_tsv(disassemble_tsv(bits, floats), floats)) << "Scientific FLOAT is not lossless";
}
