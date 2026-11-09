#include "support.h"

using namespace l2asmdisasm;

namespace
{
constexpr std::string_view versions = R"([
    {"clients":["C3","C4","C6"],"blocks":[{"fields":[{"name":"id","type":"u32"}]}]},
    {"clients":["C5"],"blocks":[{"fields":[{"name":"id","type":"u16"}]}]}
])";
} // namespace

TEST(Definitions, SelectsLayoutByClient)
{
    EXPECT_EQ(parse_schema(versions, "C4").blocks[0].fields[0].type, FieldType::U32);
    EXPECT_EQ(parse_schema(versions, "C6").blocks[0].fields[0].type, FieldType::U32);
    EXPECT_EQ(parse_schema(versions, "C5").blocks[0].fields[0].type, FieldType::U16);
    EXPECT_REJECTS(parse_schema(versions, "C7"), "No definition for client version C7; available: C3, C4, C5, C6");
    EXPECT_REJECTS(parse_schema(versions), "Client version required for multiple layouts; available: C3, C4, C5, C6");
    EXPECT_REJECTS(parse_schemas(R"([
        {"clients":["C3","C4"],"blocks":[{"fields":[{"name":"id","type":"u32"}]}]},
        {"clients":["C4","C5"],"blocks":[{"fields":[{"name":"id","type":"u16"}]}]}
    ])"),
                   "Ambiguous definition");
}

TEST(Definitions, VersionedLayouts)
{
    const auto schemas = parse_schemas(versions);
    ASSERT_EQ(schemas.size(), 2u);
    for (const auto *client : {"C3", "C4", "C5", "C6"})
        EXPECT_EQ(assemble(R"([{"id":42}])", versions, client),
                  assemble(R"([{"id":42}])", parse_schema(versions, client)));
    EXPECT_REJECTS(parse_schemas("[]"), "nonempty");
    EXPECT_REJECTS(parse_schemas(R"([{"blocks":[{"fields":[{"name":"id","type":"u32"}]}]}])"), "require clients");
}

TEST(Definitions, ClientMetadataDoesNotChangeBinaryLayout)
{
    const auto schema = parse_schema(R"({"clients":["C3","C4"],"blocks":[{"fields":[{"name":"id","type":"u32"}]}]})");
    EXPECT_EQ(schema.clients, (std::vector<std::string>{"C3", "C4"}));
    auto standalone = schema;
    standalone.clients.clear();
    EXPECT_EQ(assemble(R"([{"id":42}])", schema), assemble(R"([{"id":42}])", standalone));
    EXPECT_EQ(parse_schema(R"({"blocks":[{"fields":[{"name":"id","type":"u32"}]}]})", "C9").blocks[0].fields[0].type, FieldType::U32);
    EXPECT_REJECTS(parse_schema(R"({"clients":["C3"],"blocks":[{"fields":[{"name":"id","type":"u32"}]}]})", "C9"), "No definition for client version C9; available: C3");
    for (const auto *clients : {"[]", "{}", "[1]", "[\"C3\",\"C3\"]", "[\"../C3\"]"})
        EXPECT_THROW((void)parse_schema(std::string("{\"clients\":") + clients + R"(,"blocks":[{"fields":[{"name":"id","type":"u32"}]}]})"), std::invalid_argument);
}
