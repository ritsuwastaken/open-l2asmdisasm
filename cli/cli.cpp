#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <l2asmdisasm.h>
#include <optional>
#include <stdexcept>
#include <string_view>

#define OPTPARSE_IMPLEMENTATION
#define OPTPARSE_API static
#include "extern/optparse.h"

namespace
{
constexpr std::string_view usage =
    "Usage:\n"
    "  l2asmdisasm --help\n"
    "  l2asmdisasm disassemble -d <definition> [-v <version>] [-n <name>] [-g <names.dat>] <decrypted-dat> <output.json>\n"
    "  l2asmdisasm assemble -d <definition> [-v <version>] [-n <name>] [-g <names.dat>] [-G <names-output.dat>] <input.json> <dat-output>\n"
    "\nOptions:\n"
    "  -d <path>     JSON binary definition or definitions directory (required)\n"
    "  -v <version>  Select client version within the definition file\n"
    "  -n <name>     DAT basename for discovery (defaults to the input filename)\n"
    "  -g <path>     Decrypted L2GameDataName.dat; for Helios (EP3) and later\n"
    "  -G <path>     Output for the updated L2GameDataName.dat; assemble only, defaults to -g\n";

struct Usage : std::invalid_argument
{
    using std::invalid_argument::invalid_argument;
};

std::string read_file(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open input: " + path.string());
    std::string contents{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (stream.bad()) throw std::runtime_error("Cannot read input: " + path.string());
    return contents;
}

void write_file(const std::filesystem::path &path, std::span<const std::uint8_t> bytes)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open output: " + path.string());
    stream.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    if (!stream) throw std::runtime_error("Cannot write output: " + path.string());
}

void write_file(const std::filesystem::path &path, std::string_view text)
{
    write_file(path, std::span(reinterpret_cast<const std::uint8_t *>(text.data()), text.size()));
}

bool same_path(const std::filesystem::path &a, const std::filesystem::path &b)
{
    std::error_code error_a, error_b, error;
    const auto canonical_a = std::filesystem::weakly_canonical(a, error_a);
    const auto canonical_b = std::filesystem::weakly_canonical(b, error_b);
    if (!error_a && !error_b && canonical_a == canonical_b) return true;
    return std::filesystem::equivalent(a, b, error) && !error;
}

std::filesystem::path resolve_definition(const std::filesystem::path &directory, std::string name)
{
    if (name.find_first_of("/\\") != std::string::npos) throw std::invalid_argument("Expected a DAT basename without directories");
    for (auto &c : name)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (name.ends_with(".dat")) name.resize(name.size() - 4);
    else if (name.ends_with(".json")) name.resize(name.size() - 5);
    const auto file = directory / (name + ".json");
    if (name.empty() || !std::filesystem::is_regular_file(file)) throw std::invalid_argument("Unknown DAT filename: " + name);
    return file;
}

int convert(char **argv)
{
    const bool assembling = std::string_view(argv[0]) == "assemble";
    std::optional<std::filesystem::path> definition;
    std::optional<std::filesystem::path> names_input, names_output;
    std::optional<std::string> client_version, dat_file;
    struct optparse options;
    optparse_init(&options, argv);
    options.permute = 1;
    int option;
    while ((option = optparse(&options, "d:v:n:g:G:")) != -1)
    {
        switch (option)
        {
        case 'd':
            definition = options.optarg;
            break;
        case 'v':
            client_version = options.optarg;
            break;
        case 'n':
            dat_file = options.optarg;
            break;
        case 'g':
            names_input = options.optarg;
            break;
        case 'G':
            names_output = options.optarg;
            break;
        default:
        {
            std::string message = options.errmsg;
            if (!message.empty()) message[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(message[0])));
            throw Usage(message);
        }
        }
    }
    const char *input_argument = optparse_arg(&options);
    const char *output_argument = optparse_arg(&options);
    if (!definition || !input_argument || !output_argument || optparse_arg(&options))
        throw Usage("Expected -d <definition>, an input path, and an output path");
    const std::filesystem::path input(input_argument), output(output_argument);
    if (std::filesystem::is_directory(*definition))
    {
        *definition = resolve_definition(*definition, dat_file.value_or(input.filename().string()));
    }
    else if (dat_file) throw Usage("-n requires a definitions directory");
    if (names_output && (!assembling || !names_input)) throw Usage("-G requires assemble with -g");
    for (const auto &source : {*definition, input})
        if (same_path(source, output))
            throw std::invalid_argument("Output must differ from input and definition paths");
    if (names_input && same_path(*names_input, output))
        throw std::invalid_argument("DAT output must differ from the input name table");
    if (names_output)
        for (const auto &source : {*definition, input, output})
            if (same_path(source, *names_output))
                throw std::invalid_argument("Name table output must differ from the definition, input, and DAT output");
    const auto schema = l2asmdisasm::parse_schema(read_file(*definition), client_version.value_or(""));
    const auto contents = read_file(input);
    const auto bytes_of = [](const std::string &text)
    { return std::span(reinterpret_cast<const std::uint8_t *>(text.data()), text.size()); };
    std::optional<l2asmdisasm::NameTable> names;
    if (names_input) names = l2asmdisasm::read_name_table(bytes_of(read_file(*names_input)));
    if (assembling)
    {
        const auto original_count = names ? names->size() : 0;
        const auto bytes = l2asmdisasm::assemble(contents, schema, names ? &*names : nullptr);
        if (names && (names_output || names->size() != original_count))
            write_file(names_output.value_or(*names_input), l2asmdisasm::write_name_table(*names));
        write_file(output, bytes);
    }
    else
    {
        const auto bytes = bytes_of(contents);
        write_file(output, l2asmdisasm::disassemble(bytes, schema, names ? &*names : nullptr));
    }
    return 0;
}
} // namespace

int main(int argc, char **argv)
{
    try
    {
        if (argc == 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h"))
        {
            std::cout << usage;
            return 0;
        }
        if (argc >= 2 && (std::string_view(argv[1]) == "assemble" || std::string_view(argv[1]) == "disassemble"))
            return convert(argv + 1);
        throw Usage("Unknown command");
    }
    catch (const Usage &error)
    {
        std::cerr << "error: " << error.what() << "\n\n"
                  << usage;
        return 2;
    }
    catch (const std::exception &error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
