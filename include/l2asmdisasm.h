#ifndef L2ASMDISASM_PUBLIC_H
#define L2ASMDISASM_PUBLIC_H

#ifndef L2ASMDISASM_API
#define L2ASMDISASM_API
#endif

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace l2asmdisasm
{
/// Decoded compact index and the number of input bytes consumed.
struct CompactIndex
{
    std::int32_t value;
    std::size_t bytes_read;
};

/// Signed Unreal compact index; decoding reports how many bytes were consumed.
[[nodiscard]] L2ASMDISASM_API CompactIndex decode_compact_index(std::span<const std::uint8_t> input);

/// Encode a signed Unreal compact index using the fewest bytes.
[[nodiscard]] L2ASMDISASM_API std::vector<std::uint8_t> encode_compact_index(std::int32_t value);

enum class FieldType
{
    U32,
    I32,
    U16,
    I16,
    U8,
    I8,
    F32,
    UTF16,  ///< UTF-16LE with a 32-bit byte length.
    STRING, ///< Unreal FString: compact length, byte text or UTF-16LE.
    COMPACT,
    PADDING,
    I64,
    F64,
    GROUP
};

/// (field & mask) == value, compared as 32-bit integers.
struct Condition
{
    std::string field;
    std::int64_t value = 0;
    std::int64_t mask = -1;
};

struct Field
{
    FieldType type;
    std::string name;
    std::size_t count = 1;                          ///< Fixed array length, or padding byte count.
    bool array = false;                             ///< Keeps length-one arrays distinct from scalars.
    std::string counter;                            ///< Dynamic length: preceding integer field in this or an enclosing scope.
    std::vector<std::vector<Condition>> enabled_by; ///< OR of AND clauses.
    std::vector<Field> members;                     ///< Nested fields for GROUP.
};

struct Block
{
    static constexpr std::int64_t stored = -1, compact = -2, to_end = -3;

    std::vector<Field> fields;
    std::int64_t record_count = stored; ///< Stored u32, compact index, end of payload, or a fixed nonnegative count.
    std::string counted_by;             ///< Count nonzero values of this field; read records to the end of the payload.
};

struct Schema : Block
{
    std::vector<std::string> clients;

    std::vector<Block> blocks; ///< Additional blocks after the inherited first block.
    std::size_t magic = 0;     ///< Zero dwords before the trailer.
    bool trailer = true;       ///< SafePackage marker at the end.
};

struct EncodingOptions
{
    std::string charset = "ISO-8859-1"; ///< Charset for byte-encoded strings.
};

/// Charset names accepted by EncodingOptions::charset.
[[nodiscard]] L2ASMDISASM_API std::vector<std::string> supported_charsets();

/// Parse one layout, selecting a client when several exist.
/// @throws std::invalid_argument for invalid definitions or a missing/ambiguous client.
[[nodiscard]] L2ASMDISASM_API Schema parse_schema(std::string_view json, std::string_view client_version = {});
/// Export one layout as JSON.
[[nodiscard]] L2ASMDISASM_API std::string export_schema(const Schema &schema);
/// Parse all layouts; invalid definitions throw std::invalid_argument.
[[nodiscard]] L2ASMDISASM_API std::vector<Schema> parse_schemas(std::string_view json);
/// Export layouts with their supported client versions.
[[nodiscard]] L2ASMDISASM_API std::string export_schemas(std::span<const Schema> schemas);

/// Find directory/<dat-basename>.json; ignores ASCII case and an optional .dat/.json extension.
[[nodiscard]] L2ASMDISASM_API std::filesystem::path resolve_definition(const std::filesystem::path &directory,
                                                                       std::string_view filename);

/// Convert decrypted DAT bytes to UTF-8 JSON.
/// @throws std::invalid_argument for invalid definitions or data.
[[nodiscard]] L2ASMDISASM_API std::string disassemble(std::span<const std::uint8_t> input, const Schema &schema,
                                                      const EncodingOptions &options = {});
/// Convert UTF-8 JSON to decrypted DAT bytes.
/// @throws std::invalid_argument for invalid definitions or data.
[[nodiscard]] L2ASMDISASM_API std::vector<std::uint8_t> assemble(std::string_view json, const Schema &schema,
                                                                 const EncodingOptions &options = {});

} // namespace l2asmdisasm

#endif // L2ASMDISASM_PUBLIC_H
