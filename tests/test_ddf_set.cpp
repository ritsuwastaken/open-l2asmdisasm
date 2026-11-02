#include "support.h"

#include <filesystem>
#include <fstream>
#include <iterator>

TEST(DdfSet, AllDefinitionsParse)
{
    std::size_t parsed = 0;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(L2ASMDISASM_DDF_DIR))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".ddf") continue;
        std::ifstream stream(entry.path(), std::ios::binary);
        const std::string definition{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        EXPECT_NO_THROW((void)l2asmdisasm::parse_ddf(definition)) << entry.path().string();
        ++parsed;
    }
    EXPECT_GT(parsed, 0u);
}
