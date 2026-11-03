#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
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
    "  l2asmdisasm cntr encode <signed-decimal>\n"
    "  l2asmdisasm cntr decode <hex-bytes>\n"
    "  l2asmdisasm disassemble -d <definition.json> [-a <charset>] <decrypted-dat> <output.json>\n"
    "  l2asmdisasm assemble -d <definition.json> [-a <charset>] <input.json> <dat-output>\n"
    "\nOptions:\n"
    "  -d <path>     JSON binary definition or definitions directory (required)\n"
    "  -v <version>  Select client version within the definition file\n"
    "  -n <name>     DAT basename for discovery (defaults to the input filename)\n"
    "  -a <charset>  Charset for byte ASCF (default ISO-8859-1)\n";

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

template <typename T>
T parse(std::string_view text, int base)
{
    T value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw std::invalid_argument("Invalid number");
    return value;
}
std::string charset_list()
{
    std::string result;
    for (const auto &name : l2asmdisasm::supported_charsets())
        result += (result.empty() ? "" : ", ") + name;
    return result;
}

int convert(char **argv)
{
    const bool assembling = std::string_view(argv[0]) == "assemble";
    l2asmdisasm::TextOptions text;
    std::optional<std::filesystem::path> definition;
    std::optional<std::string> client_version, dat_file;
    struct optparse options;
    optparse_init(&options, argv);
    options.permute = 1;
    int option;
    while ((option = optparse(&options, "d:a:v:n:")) != -1)
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
        case 'a':
            text.charset = options.optarg;
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
        throw Usage("Expected -d <definition.json>, an input path, and an output path");
    const std::filesystem::path input(input_argument), output(output_argument);
    if (std::filesystem::is_directory(*definition))
    {
        *definition = l2asmdisasm::resolve_definition(*definition, dat_file.value_or(input.filename().string()));
    }
    else if (dat_file) throw Usage("-n requires a definitions directory");
    for (const auto &source : {*definition, input})
        if (same_path(source, output))
            throw std::invalid_argument("Output must differ from input and definition paths");
    const auto schema = l2asmdisasm::parse_schema(read_file(*definition), client_version.value_or(""));
    const auto contents = read_file(input);
    try
    {
        if (assembling) write_file(output, l2asmdisasm::assemble(contents, schema, text));
        else
        {
            const auto bytes = std::span(reinterpret_cast<const std::uint8_t *>(contents.data()), contents.size());
            const auto converted = l2asmdisasm::disassemble(bytes, schema, text);
            write_file(output, converted);
        }
    }
    catch (const std::invalid_argument &error)
    {
        if (!std::string_view(error.what()).starts_with("Unsupported charset")) throw;
        throw std::invalid_argument(std::string(error.what()) + "; supported: " + charset_list());
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
        if (argc != 4 || std::string_view(argv[1]) != "cntr") throw Usage("Unknown command");
        const std::string_view operation(argv[2]);
        const std::string_view argument(argv[3]);
        if (operation == "encode")
        {
            const auto bytes = l2asmdisasm::encode_compact_index(parse<std::int32_t>(argument, 10));
            for (const auto byte : bytes)
                std::cout << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned>(byte);
            std::cout << '\n';
            return 0;
        }
        if (operation == "decode")
        {
            if (argument.empty() || argument.size() % 2 != 0 || argument.size() > 10)
                throw std::invalid_argument("Expected one to five hex bytes, without spaces or a 0x prefix");
            std::vector<std::uint8_t> bytes;
            for (std::size_t i = 0; i < argument.size(); i += 2)
                bytes.push_back(static_cast<std::uint8_t>(parse<unsigned>(argument.substr(i, 2), 16)));
            const auto decoded = l2asmdisasm::decode_compact_index(bytes);
            if (decoded.bytes_read != bytes.size()) throw std::invalid_argument("Trailing bytes after compact index");
            std::cout << decoded.value << '\n';
            return 0;
        }
        throw Usage("Unknown cntr operation");
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
