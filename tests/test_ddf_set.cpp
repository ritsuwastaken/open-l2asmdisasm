#include <l2asmdisasm.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: l2asmdisasm_ddf_set_tests <ddf-directory>\n";
        return 2;
    }
    std::size_t parsed = 0, failed = 0;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(argv[1]))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".ddf") continue;
        std::ifstream stream(entry.path(), std::ios::binary);
        const std::string definition{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        try
        {
            (void)l2asmdisasm::parse_ddf(definition);
            ++parsed;
        }
        catch (const std::exception &error)
        {
            ++failed;
            std::cerr << entry.path().string() << ": " << error.what() << '\n';
        }
    }
    return parsed != 0 && failed == 0 ? 0 : 1;
}
