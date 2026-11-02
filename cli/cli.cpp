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
    "  l2asmdisasm disassemble -d <ddf> [-e <updated-ddf>] [-o g|f|e] [-q] [-l] [-f] [-a <charset>]\n"
    "                          <decrypted-dat> <text-output>\n"
    "  l2asmdisasm assemble -d <ddf> [-q] [-l] [-f] [-a <charset>] <text-input> <dat-output>\n"
    "\nOptions:\n"
    "  -d <ddf>      DAT definition file (required)\n"
    "  -e <ddf>      disassemble: export the DDF with updated SOFT* widths\n"
    "  -o g|f|e      disassemble: FLOAT notation; g = shortest round trip (default),\n"
    "                f = legacy fixed (lossy), e = legacy scientific\n"
    "  -q            no header line, overriding HEADER\n"
    "  -l            legacy byte-oriented text with basic transliteration (not UTF-8)\n"
    "  -f            disassemble: write every ASCF with an a, hint;\n"
    "                assemble: encode every ASCF as 8-bit\n"
    "  -a <charset>  charset for 8-bit ASCF (default ISO-8859-1; ignored with -l)\n";

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
    std::optional<std::filesystem::path> definition, exported;
    bool no_header = false;
    struct optparse options;
    optparse_init(&options, argv);
    options.permute = 1;
    int option;
    while ((option = optparse(&options, assembling ? "d:qlfa:" : "d:e:o:qlfa:")) != -1)
    {
        switch (option)
        {
        case 'd':
            definition = options.optarg;
            break;
        case 'e':
            exported = options.optarg;
            break;
        case 'o':
            if (std::string_view(options.optarg) == "g") text.float_format = l2asmdisasm::FloatFormat::Shortest;
            else if (std::string_view(options.optarg) == "f") text.float_format = l2asmdisasm::FloatFormat::Fixed;
            else if (std::string_view(options.optarg) == "e") text.float_format = l2asmdisasm::FloatFormat::Scientific;
            else throw Usage("-o expects g, f, or e");
            break;
        case 'q':
            no_header = true;
            break;
        case 'l':
            text.legacy = true;
            break;
        case 'f':
            text.force_ascii = true;
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
        throw Usage("Expected -d <ddf>, an input path, and an output path");
    const std::filesystem::path input(input_argument), output(output_argument);
    for (const auto &source : {*definition, input})
        if (same_path(source, output) || (exported && same_path(source, *exported)))
            throw std::invalid_argument("Output must differ from input and DDF paths");
    if (exported && same_path(output, *exported)) throw std::invalid_argument("Text and DDF outputs must differ");
    auto schema = l2asmdisasm::parse_ddf(read_file(*definition));
    const bool header = schema.header;
    if (no_header && !schema.blocks.empty()) throw std::invalid_argument("-q can't be used with several blocks");
    if (no_header) schema.header = false;
    const auto contents = read_file(input);
    try
    {
        if (assembling) write_file(output, l2asmdisasm::assemble(contents, schema, text));
        else
        {
            const auto bytes = std::span(reinterpret_cast<const std::uint8_t *>(contents.data()), contents.size());
            const auto converted = l2asmdisasm::disassemble(bytes, schema, &schema, text);
            write_file(output, converted);
            schema.header = header;
            if (exported) write_file(*exported, l2asmdisasm::export_ddf(schema));
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
