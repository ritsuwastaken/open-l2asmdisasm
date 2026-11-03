#include "support.h"
#include <chrono>
#include <fstream>

using namespace l2asmdisasm;

namespace
{
struct Definitions
{
    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 ("l2asmdisasm-definitions-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    Definitions() { std::filesystem::create_directories(root / "item"); }
    ~Definitions()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    void write(std::string_view filename, std::string_view clients, std::string_view type = "u32")
    {
        std::ofstream(root / "item" / filename) << "{\"clients\":" << clients
                                                << ",\"blocks\":[{\"fields\":[{\"name\":\"id\",\"type\":\"" << type << "\"}]}]}";
    }
};
} // namespace

TEST(Definitions, DiscoversSupportedVersionsWithoutIndex)
{
    Definitions definitions;
    definitions.write("C3.json", R"(["C3","C4","C6"])");
    definitions.write("C5.json", R"(["C5"])", "u16");
    EXPECT_EQ(resolve_definition(definitions.root, "ITEM.DAT", "C4"), definitions.root / "item" / "C3.json");
    EXPECT_EQ(resolve_definition(definitions.root, "item.json", "C6"), definitions.root / "item" / "C3.json");
    EXPECT_EQ(resolve_definition(definitions.root, "item", "C5"), definitions.root / "item" / "C5.json");
    EXPECT_REJECTS(resolve_definition(definitions.root, "missing", "C3"), "Unknown DAT filename");
    EXPECT_REJECTS(resolve_definition(definitions.root, "item", "C7"), "No definition");
    EXPECT_REJECTS(resolve_definition(definitions.root, "../item", "C3"), "without directories");
    definitions.write("C5.json", R"(["C4","C5"])", "u16");
    EXPECT_REJECTS(resolve_definition(definitions.root, "item", "C4"), "Ambiguous definition");
}

TEST(Definitions, ClientMetadataRoundTripsAndDoesNotChangeBinaryLayout)
{
    const auto schema = parse_schema(R"({"clients":["C3","C4"],"blocks":[{"fields":[{"name":"id","type":"u32"}]}]})");
    EXPECT_EQ(parse_schema(export_schema(schema)).clients, schema.clients);
    auto standalone = schema;
    standalone.clients.clear();
    EXPECT_EQ(assemble(R"([{"id":42}])", schema), assemble(R"([{"id":42}])", standalone));
    for (const auto *clients : {"[]", "{}", "[1]", "[\"C3\",\"C3\"]", "[\"../C3\"]"})
        EXPECT_THROW((void)parse_schema(std::string("{\"clients\":") + clients + R"(,"blocks":[{"fields":[{"name":"id","type":"u32"}]}]})"), std::invalid_argument);
}
