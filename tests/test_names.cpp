#include "support.h"

using namespace l2asmdisasm;

namespace
{
constexpr std::string_view definition = R"({"blocks":[{"fields":[
    {"name":"name","type":"u32","format":"name-ref"},
    {"name":"number","type":"u32"}
]}]})";
}

TEST(Names, TableBinaryLayoutAndTermination)
{
    const NameTable names{std::string("A\0", 2), "", "A"};
    const std::vector<std::uint8_t> bytes{
        3, 0, 0, 0, 4, 0, 0, 0, 'A', 0, 0, 0,
        0, 0, 0, 0, 2, 0, 0, 0, 'A', 0,
        12, 'S', 'a', 'f', 'e', 'P', 'a', 'c', 'k', 'a', 'g', 'e', 0};
    EXPECT_EQ(write_name_table(names), bytes);
    EXPECT_EQ(read_name_table(bytes), names);
    EXPECT_EQ(write_name_table(read_name_table(bytes)), bytes);
    const NameTable unicode{"汉字😀", std::string("a\0b\0", 4)};
    EXPECT_EQ(read_name_table(write_name_table(unicode)), unicode);
    for (std::size_t n = 0; n < bytes.size(); ++n)
        EXPECT_THROW((void)read_name_table(std::span(bytes).first(n)), std::invalid_argument);
    auto invalid = bytes;
    invalid[0] = 255;
    EXPECT_REJECTS(read_name_table(invalid), "Name count");
    invalid = bytes;
    invalid.insert(invalid.end() - 13, 0);
    EXPECT_REJECTS(read_name_table(invalid), "trailing");
    EXPECT_REJECTS(write_name_table(NameTable{{std::string(1, '\xff')}}), "UTF-8");
}

TEST(Names, PreserveDuplicatesUnknownIndexesAndOrdinaryIntegers)
{
    const auto schema = parse_schema(definition);
    NameTable names{"Mesh", "Mesh", "Other"};
    const auto original = names;
    const auto bytes = assemble(R"([
        {"name":1,"number":0},{"name":4294967295,"number":1},{"name":99,"number":2}
    ])",
                                schema);
    const auto text = disassemble(bytes, schema, &names);
    EXPECT_NE(text.find("\"index\": 1"), std::string::npos);
    EXPECT_NE(text.find("\"value\": \"Mesh\""), std::string::npos);
    EXPECT_NE(text.find("\"name\": 4294967295"), std::string::npos);
    EXPECT_NE(text.find("\"number\": 0"), std::string::npos);
    EXPECT_EQ(assemble(text, schema, &names), bytes);
    EXPECT_EQ(names, original);
    EXPECT_EQ(assemble(disassemble(bytes, schema), schema), bytes);
    EXPECT_REJECTS(assemble(text, schema), "Name table required");
    auto raw = write_name_table(names);
    EXPECT_EQ(disassemble(bytes, definition, {}, raw), text);
    EXPECT_EQ(assemble(text, definition, {}, &raw), bytes);
    EXPECT_EQ(read_name_table(raw), original);
    auto table = names;
    EXPECT_EQ(assemble(R"([{"name":"New","number":0}])", definition, {}, &raw),
              assemble(R"([{"name":"New","number":0}])", schema, &table));
    EXPECT_EQ(read_name_table(raw), table);
}

TEST(Names, EditAppendReuseAndRollback)
{
    const auto schema = parse_schema(definition);
    NameTable names{"Mesh", "Mesh", "Other"};
    const auto bytes = assemble(R"([
        {"name":{"index":1,"value":"Other"},"number":1},
        {"name":{"index":0,"value":"New"},"number":2},
        {"name":"New","number":3},
        {"name":"mesh","number":4}
    ])",
                                schema, &names);
    EXPECT_EQ(names, (NameTable{"Mesh", "Mesh", "Other", "New"}));
    EXPECT_EQ(bytes, assemble(R"([
        {"name":2,"number":1},{"name":3,"number":2},
        {"name":3,"number":3},{"name":0,"number":4}
    ])",
                              schema));
    const auto before = names;
    EXPECT_REJECTS(assemble(R"([{"name":"AppendedBeforeError","number":-1}])", schema, &names), "overflow");
    EXPECT_EQ(names, before);
    for (const auto *bad : {R"({"index":-1,"value":"x"})", R"({"index":4294967296,"value":"x"})",
                            R"({"index":0})", R"({"index":0,"value":1})", R"({"index":0,"value":"x","extra":1})"})
    {
        EXPECT_THROW((void)assemble(std::string("[{\"name\":") + bad + ",\"number\":0}]", schema, &names), std::invalid_argument);
        EXPECT_EQ(names, before);
    }
}

TEST(Names, NestedArraysAndSchemaValidation)
{
    const auto schema = parse_schema(R"({"blocks":[{"fields":[
        {"name":"n","type":"u8"},
        {"name":"entries","type":"group","count":"n","fields":[
            {"name":"refs","type":"u32","count":2,"format":"name-ref"}
        ]}
    ]}]})");
    NameTable names{"One", "Two"};
    const auto bytes = assemble(R"([{"entries":[{"refs":[0,1]},{"refs":[1,0]}]}])", schema);
    EXPECT_EQ(assemble(disassemble(bytes, schema, &names), schema, &names), bytes);
    const auto exported = export_schema(schema);
    EXPECT_EQ(parse_schema(exported).blocks[0].fields[1].members[0].format, FieldFormat::NameRef);
    EXPECT_EQ(export_schema(parse_schema(exported)), exported);
    EXPECT_REJECTS(parse_schema(R"({"blocks":[{"fields":[{"name":"x","type":"string","format":"name-ref"}]}]})"), "requires u32");
    EXPECT_REJECTS(parse_schema(R"({"blocks":[{"fields":[{"name":"x","type":"u32","format":"other"}]}]})"), "Unknown field format");
    EXPECT_REJECTS(parse_schema(R"({"blocks":[{"fields":[
        {"name":"n","type":"u32","format":"name-ref"},
        {"name":"values","type":"u8","count":"n"}
    ]}]})"),
                   "cannot be a counter");
}

TEST(Names, SignedReferencePreservesSentinel)
{
    auto schema = parse_schema(definition);
    schema.blocks[0].fields[0].type = FieldType::I32;
    NameTable names{"Mesh"};
    const auto bytes = assemble(R"([{"name":-1,"number":0},{"name":0,"number":1}])", schema);
    const auto text = disassemble(bytes, schema, &names);
    EXPECT_NE(text.find("\"name\": -1"), std::string::npos);
    EXPECT_EQ(assemble(text, schema, &names), bytes);
    EXPECT_EQ(assemble(R"([{"name":"Mesh","number":0}])", schema, &names),
              assemble(R"([{"name":0,"number":0}])", schema));
    EXPECT_EQ(parse_schema(export_schema(schema)).blocks[0].fields[0].format, FieldFormat::NameRef);
}
