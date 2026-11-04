#include "support.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <set>

using namespace l2asmdisasm;

namespace
{
void matches_tsv(const Schema &schema, std::string_view tsv)
{
    const auto bytes = assemble_tsv(tsv, schema);
    const auto json = disassemble(bytes, schema);
    EXPECT_EQ(assemble(json, schema), bytes) << json;
    const auto migrated = parse_schema(export_schema(schema));
    EXPECT_EQ(disassemble(bytes, migrated), json);
    EXPECT_EQ(assemble(json, migrated), bytes);
}
} // namespace

TEST(Json, NativeDefinitionsAndDynamicArrays)
{
    const auto schema = parse_schema(R"({"blocks":[{"fields":[{"name":"id","type":"u32"},{"name":"name","type":"utf16"},{"name":"n","type":"compact"},{"name":"values","type":"u16","count":"n"}]}]})");
    const auto data = R"([{"values":[10,20,30],"name":"Item\n\t\u0000\\\"é","id":1},
                         {"id":2,"name":"","values":[]}])";
    const auto bytes = assemble(data, schema);
    const auto json = disassemble(bytes, schema);
    EXPECT_EQ(assemble(json, schema), bytes);
    EXPECT_EQ(export_schema(parse_schema(export_schema(schema))), export_schema(schema));
    EXPECT_EQ(schema.fields.back().width, 0u);
}

TEST(Json, ExactPrimitiveBinaryCompatibility)
{
    matches_tsv(parse_schema(R"({"trailer":false,"magic":2,"blocks":[{"fields":[{"name":"a","type":"u32"},{"name":"b","type":"hex32"},{"name":"c","type":"i32"},{"name":"d","type":"u16"},{"name":"e","type":"i16"},{"name":"f","type":"u8"},{"name":"g","type":"hex8"},{"name":"h","type":"i8"},{"name":"i","type":"f32"},{"name":"j","type":"f64"},{"name":"k","type":"i64"},{"name":"l","type":"compact"},{"name":"m","type":"utf16"},{"name":"n","type":"string"},{"name":"o","type":"string"},{"name":"pad","type":"padding","size":3}]}]})"),
                "a\tb\tc\td\te\tf\tg\th\ti\tj\tk\tl\tm\tn\to\tpad{3}\n"
                "4294967295\tFFFFFFFF\t-2147483648\t65535\t-32768\t255\tFF\t-128\t-0\t1e-300\t-9223372036854775808\t-9000\tA\\nB\\0\ta,é\tu,世界\t255\n");
    matches_tsv(parse_schema(R"({"blocks":[{"fields":[{"name":"a","type":"f32"},{"name":"b","type":"f32"},{"name":"c","type":"f64"},{"name":"d","type":"f64"}]}]})"),
                "a\tb\tc\td\nNaN(0x7FC00001)\t-inf\tNaN(0xFFF8000000000012)\tinf\n");
}

TEST(Json, GroupsConditionsAndParentReferences)
{
    const auto schema = parse_schema(R"({"blocks":[{"fields":[{"name":"flags","type":"u32"},{"name":"n","type":"u32"},{"name":"rows","type":"group","count":"n","fields":[{"name":"id","type":"u32"},{"name":"bytes","type":"u8","count":"n"},{"name":"text","type":"utf16","enabledBy":[[{"field":"id","value":1}]]}]}]}]})");
    const auto data = R"([{"flags":9,"n":2,"rows":[{"id":1,"bytes":[1,2],"text":"yes"},{"id":0,"bytes":[3,4]}]}])";
    const auto bytes = assemble(data, schema);
    EXPECT_EQ(assemble(disassemble(bytes, schema), schema), bytes);
    EXPECT_REJECTS(assemble(R"([{"flags":9,"n":1,"rows":[{"id":0,"bytes":[1],"text":"bad"}]}])", schema), "Disabled field");
    EXPECT_REJECTS(assemble(R"([{"flags":9,"n":1,"rows":[]}])", schema), "Array length");
}

TEST(Json, NestedMeshAndMaterialArrays)
{
    matches_tsv(([]
                 {
                     auto schema = parse_schema(R"({"blocks":[{"fields":[{"name":"a","type":"group","fields":[{"name":"meshCount","type":"i32"},{"name":"meshes","type":"utf16","count":"meshCount"},{"name":"textureCount","type":"i32"},{"name":"textures","type":"utf16","count":"textureCount"}]},{"name":"b","type":"group","fields":[{"name":"meshCount","type":"i32"},{"name":"meshes","type":"group","count":"meshCount","fields":[{"name":"name","type":"utf16"},{"name":"byte","type":"u8"},{"name":"value","type":"u32"}]},{"name":"textureCount","type":"i32"},{"name":"textures","type":"utf16","count":"textureCount"}]},{"name":"c","type":"group","fields":[{"name":"meshCount","type":"i32"},{"name":"meshes","type":"group","count":"meshCount","fields":[{"name":"name","type":"utf16"},{"name":"bytes","type":"u8","count":2}]},{"name":"textureCount","type":"i32"},{"name":"textures","type":"utf16","count":"textureCount"},{"name":"extraTexture","type":"utf16"}]},{"name":"d","type":"group","fields":[{"name":"count","type":"i32"},{"name":"entries","type":"group","count":"count","fields":[{"name":"id","type":"i32"},{"name":"count","type":"i32"}]}]},{"name":"e","type":"group","fields":[{"name":"count","type":"i32"},{"name":"extra","type":"i32"},{"name":"entries","type":"group","count":"count","fields":[{"name":"id","type":"i32"},{"name":"count","type":"i32"}]}]}]}]})");
                     schema.fields[0].members[1].width = 1;
                     schema.fields[0].members[3].width = 1;
                     schema.fields[1].members[1].width = 1;
                     schema.fields[1].members[3].width = 1;
                     schema.fields[2].members[1].width = 1;
                     schema.fields[2].members[3].width = 1;
                     schema.fields[3].members[1].width = 1;
                     schema.fields[4].members[2].width = 1;
                     schema.header = false;
                     return schema; }()),
                "1\tmesh\t1\ttex\t1\tmesh2\t2\t3\t1\ttex2\t1\tmesh3\t4\t5\t1\ttex3\textra\t1\t8\t9\t1\t10\t11\t12\n");
    const auto schema = parse_schema(R"({"blocks":[{"fields":[{"name":"n","type":"u32"},{"name":"m","type":"group","count":"n","fields":[{"name":"meshCount","type":"i32"},{"name":"meshes","type":"utf16","count":"meshCount"},{"name":"textureCount","type":"i32"},{"name":"textures","type":"utf16","count":"textureCount"}]},{"name":"a","type":"group","fields":[{"name":"count","type":"i32"},{"name":"extra","type":"i32"},{"name":"entries","type":"group","count":"count","fields":[{"name":"id","type":"i32"},{"name":"count","type":"i32"}]}]}]}]})");
    const auto bytes = assemble(R"([{"m":[{"meshes":[],"textures":[]}],"a":{"extra":99,"entries":[]}}])", schema);
    EXPECT_EQ(assemble(disassemble(bytes, schema), schema), bytes);
}

TEST(Json, BlocksAndRecordCountModes)
{
    matches_tsv(parse_schema(R"({"blocks":[{"recordCount":1,"fields":[{"name":"a","type":"u32"}]},{"recordCount":"compact","fields":[{"name":"b","type":"u8"}]},{"recordCount":"eof","fields":[{"name":"c","type":"u32"}]}]})"),
                "a\n1\n\\block\nb\n2\n3\n\\block\nc\n4\n");
    matches_tsv(parse_schema(R"({"blocks":[{"recordCount":{"nonzero":"flag"},"fields":[{"name":"flag","type":"u32"},{"name":"id","type":"u32"}]}]})"), "flag\tid\n1\t2\n0\t3\n1\t4\n");
    const auto schema = parse_schema(R"({"trailer":false,"blocks":[{"recordCount":0,"fields":[{"name":"a","type":"u8"}]},{"recordCount":"compact","fields":[{"name":"b","type":"u16"}]}]})");
    const auto bytes = assemble(R"({"blocks":[[],[{"b":8}]]})", schema);
    EXPECT_EQ(assemble(disassemble(bytes, schema), schema), bytes);
    EXPECT_REJECTS(assemble(R"({"blocks":[[]]})", schema), "block count");
}

TEST(Json, IgnoresLossyTsvPresentation)
{
    auto schema = ([]
                   {
                       auto schema = parse_schema(R"({"blocks":[{"fields":[{"name":"a","type":"u32"},{"name":"b","type":"u32"},{"name":"m","type":"group","fields":[{"name":"meshCount","type":"i32"},{"name":"meshes","type":"utf16","count":"meshCount"},{"name":"textureCount","type":"i32"},{"name":"textures","type":"utf16","count":"textureCount"}]}]}]})");
                       schema.header = false;
                       schema.separator = "|";
                       schema.fields[0].order = -1;
                       schema.fields[1].order = 1;
                       schema.fields[1].skip_if.push_back({0, 0, 0.0, 0.0});
                       return schema; }());
    const auto data = R"([{"a":42,"b":0,"m":{"meshCount":0,"meshes":[],"textureCount":0,"textures":[]}}])";
    TextOptions options;
    options.legacy = options.force_ascii = true;
    options.float_format = FloatFormat::Fixed;
    const auto bytes = assemble(data, schema, options);
    const auto result = disassemble(bytes, schema, options);
    EXPECT_NE(result.find("\"a\": 42"), std::string::npos);
    EXPECT_NE(result.find("\"b\": 0"), std::string::npos);
    EXPECT_EQ(assemble(result, schema), bytes);
}

TEST(Json, DuplicateFieldNamesHaveDistinctKeys)
{
    const auto schema = parse_schema(R"({"blocks":[{"fields":[{"name":"a","type":"u32"},{"name":"a","type":"u32"},{"name":"a","type":"u32"},{"name":"n","type":"u32"},{"name":"n","type":"u32"},{"name":"v","type":"u8","count":"n"}]}]})");
    const auto data = R"([{"a":1,"a#2":2,"a#3":3,"n":1,"n#2":9,"v":[8]}])";
    const auto bytes = assemble(data, schema);
    const auto result = disassemble(bytes, schema);
    EXPECT_NE(result.find("\"a#2\": 2"), std::string::npos);
    EXPECT_EQ(assemble(result, schema), bytes);
}

TEST(Json, RejectsMalformedDataAndDefinitions)
{
    const auto schema = parse_schema(R"({"blocks":[{"fields":[{"name":"id","type":"u8"}]}]})");
    for (const auto *data : {"", "{}", "[{}]", "[{\"id\":256}]", "[{\"id\":-1}]", "[{\"id\":1.5}]",
                             "[{\"id\":true}]", "[{\"id\":null}]", "[{\"id\":1,\"extra\":2}]", "[{\"id\":1,\"id\":2}]"})
        EXPECT_THROW((void)assemble(data, schema), std::invalid_argument) << data;
    for (const auto *definition : {"{}", "{\"blocks\":[]}", "{\"version\":2,\"blocks\":[]}",
                                   R"({"blocks":[{"fields":[{"name":"a","type":"u128"}]}]})",
                                   R"({"blocks":[{"fields":[{"name":"a","type":"u8","count":-1}]}]})",
                                   R"({"blocks":[{"fields":[{"name":"a","type":"u8","count":"n"},{"name":"n","type":"u8"}]}]})",
                                   R"({"blocks":[{"fields":[{"name":"a","type":"u8","typo":1}]}]})"})
        EXPECT_THROW((void)parse_schema(definition), std::invalid_argument) << definition;
    auto bytes = assemble("[{\"id\":1}]", schema);
    bytes.back() ^= 1;
    EXPECT_REJECTS(disassemble(bytes, schema), "trailer");
    bytes.resize(3);
    EXPECT_REJECTS(disassemble(bytes, schema), "Truncated");
    const auto wide = parse_schema(R"({"blocks":[{"fields":[{"name":"a","type":"i64"},{"name":"b","type":"f32"}]}]})");
    EXPECT_REJECTS(assemble(R"([{"a":9007199254740993,"b":1}])", wide), "decimal string");
    EXPECT_REJECTS(assemble(R"([{"a":"0","b":1e100}])", wide), "overflow");
}

TEST(Json, AllBundledDefinitionsExportAndReparse)
{
    std::size_t parsed = 0;
    std::set<std::pair<std::string, std::string>> routes;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(L2ASMDISASM_DEFINITION_DIR))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        std::ifstream stream(entry.path(), std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        const auto schemas = parse_schemas(text);
        EXPECT_EQ(text.find("\"$schema\""), std::string::npos) << entry.path().string();
        EXPECT_EQ(entry.path().parent_path(), std::filesystem::path(L2ASMDISASM_DEFINITION_DIR));
        for (const auto &schema : schemas)
        {
            EXPECT_FALSE(schema.clients.empty()) << entry.path().string();
            for (const auto &client : schema.clients)
                EXPECT_TRUE(routes.emplace(entry.path().stem().string(), client).second) << entry.path().string() << ": " << client;
        }
        const auto exported = export_schemas(schemas);
        EXPECT_EQ(export_schemas(parse_schemas(exported)), exported) << entry.path().string();
        ++parsed;
    }
    EXPECT_GT(parsed, 0u);
}

TEST(Json, StrictParsingWithJsonCpp)
{
    const auto schema = parse_schema(R"({"blocks":[{"fields":[{"name":"value","type":"utf16"}]}]})");
    for (const auto *text : {
             R"([{"value":"a","\u0076alue":"b"}])",
             R"([{"value":"\uD800\u0041"}])", R"([{"value":"\uDC00"}])",
             R"([{"value":"\uD800"}])", "[{\"value\":\"raw\nnewline\"}]",
             R"([{"value":"ok",}])", R"([/* comment */{"value":"ok"}])",
             R"([{"value":12}])", R"([{"value":null}])", R"([{"value":"ok"}] false)"})
        EXPECT_THROW((void)assemble(text, schema), std::invalid_argument) << text;
    EXPECT_REJECTS(assemble(std::string("[{\"value\":\"") + char(0xff) + "\"}]", schema), "UTF-8");
    const auto valid = assemble(R"([{"value":"\uD83D\uDE00\u0000"}])", schema);
    EXPECT_EQ(assemble(disassemble(valid, schema), schema), valid);

    const auto floats = parse_schema(R"({"blocks":[{"fields":[{"name":"value","type":"f64"}]}]})");
    for (const auto *number : {"01", "-01", "1.", "1e", "1e+", "NaN", "Infinity", "+1", "1e10000"})
        EXPECT_THROW((void)assemble(std::string("[{\"value\":") + number + "}]", floats), std::invalid_argument) << number;
    EXPECT_REJECTS(parse_schema(R"({"trailer":1,"blocks":[{"fields":[{"name":"a","type":"u8"}]}]})"), "boolean");
}

TEST(Json, FloatBitsRoundTrip)
{
    const auto schema = parse_schema(R"({"trailer":false,"blocks":[{"fields":[{"name":"f","type":"f32"},{"name":"d","type":"f64"}]}]})");
    const std::uint32_t f[] = {0, 0x80000000, 1, 0x007fffff, 0x00800000, 0x3dcccccd, 0x7f7fffff, 0x7f800000, 0xff800000, 0x7fc00001, 0x7f800001};
    const std::uint64_t d[] = {0, 0x8000000000000000ULL, 1, 0x000fffffffffffffULL, 0x0010000000000000ULL, 0x3fb999999999999aULL,
                               0x7fefffffffffffffULL, 0x7ff0000000000000ULL, 0xfff0000000000000ULL, 0x7ff8000000000001ULL, 0x7ff0000000000001ULL};
    std::vector<std::uint8_t> bytes;
    const auto put = [&](std::uint64_t bits, unsigned size)
    {
        for (unsigned i = 0; i < size; ++i)
            bytes.push_back(static_cast<std::uint8_t>(bits >> (i * 8)));
    };
    constexpr std::size_t count = 8192;
    put(count, 4);
    std::mt19937_64 random(42);
    for (std::size_t i = 0; i < count; ++i)
    {
        put(i < std::size(f) ? f[i] : static_cast<std::uint32_t>(random()), 4);
        put(i < std::size(d) ? d[i] : random(), 8);
    }
    const auto json = disassemble(bytes, schema);
    EXPECT_EQ(assemble(json, schema), bytes);
}

TEST(Json, InferredCounters)
{
    const auto shared = parse_schema(R"({"blocks":[{"fields":[{"name":"n","type":"u8"},{"name":"a","type":"u32","count":"n"},{"name":"b","type":"utf16","count":"n"}]}]})");
    const auto bytes = assemble(R"([{"a":[10,20],"b":["x","y"]}])", shared);
    EXPECT_EQ(bytes[4], 2);
    EXPECT_EQ(assemble(R"([{"n":2,"a":[10,20],"b":["x","y"]}])", shared), bytes);
    EXPECT_EQ(assemble(disassemble(bytes, shared), shared), bytes);
    EXPECT_REJECTS(assemble(R"([{"a":[1,2],"b":["x"]}])", shared), "different lengths");
    EXPECT_REJECTS(assemble(R"([{"n":1,"a":[1,2],"b":["x"]}])", shared), "Array length");

    const auto conditional = parse_schema(R"({"blocks":[{"fields":[{"name":"flag","type":"u8"},{"name":"n","type":"i32"},{"name":"values","type":"u8","count":"n","enabledBy":[[{"field":"flag","value":1}]]}]}]})");
    const auto records = assemble(R"([{"flag":0,"n":-7},{"flag":1,"values":[1,2]}])", conditional);
    EXPECT_EQ(assemble(disassemble(records, conditional), conditional), records);
    EXPECT_REJECTS(assemble(R"([{"flag":0}])", conditional), "Missing JSON property: n");
}
