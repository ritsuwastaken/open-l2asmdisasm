#ifndef L2ASMDISASM_PUBLIC_H
#define L2ASMDISASM_PUBLIC_H

#ifndef L2ASMDISASM_API
#define L2ASMDISASM_API
#endif

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace l2asmdisasm
{
struct CompactIndex
{
    std::int32_t value;
    std::size_t bytes_read; // bytes consumed from the input
};

/**
 * @brief Decode a signed Unreal compact index.
 * @throws std::invalid_argument on truncated or overflowing input
 */
[[nodiscard]] L2ASMDISASM_API CompactIndex decode_compact_index(std::span<const std::uint8_t> input);

/**
 * @brief Encode a signed Unreal compact index using the minimal number of bytes.
 */
[[nodiscard]] L2ASMDISASM_API std::vector<std::uint8_t> encode_compact_index(std::int32_t value);

enum class FieldType
{
    UINT,
    HEX,
    INT,
    UWORD,
    WORD,
    UCHAR,
    CHEX,
    CHAR,
    FLOAT,
    UNICODE,
    ASCF,
    CNTR,
    FILLER,
    INT64,
    DOUBLE,
    GROUP,
    MTX,
    MTX2,
    MTX3,
    MAT,
    MAT2
};

struct Condition // ENBBY term: (field & mask) == value, compared as 32-bit
{
    std::string field; // preceding integer field in scope; arrays use their first element
    std::int64_t value = 0;
    std::int64_t mask = -1;
};

struct Field
{
    FieldType type;
    std::string name;                               // may repeat; references resolve to the first field with the name
    std::size_t count = 1;                          // fixed array length, or FILLER byte count
    bool array = false;                             // declared with [...]
    std::string counter;                            // dynamic array: preceding integer field holding the length
    std::size_t soft = 0, softm = 0, softt = 0;     // SOFT/SOFTM/SOFTT text column widths
    std::vector<std::vector<Condition>> enabled_by; // ENBBY: OR of clauses, each an AND of conditions
    std::optional<std::int32_t> order;              // ORD, -1 (OFF) hides the field; disassembly only
    struct Skip                                     // SKIPIF: blank elements [first, last] whose value is in [low, high]
    {
        std::size_t first, last;
        double low, high;
    };
    std::vector<Skip> skip_if; // disassembly only
    std::vector<Field> members; // GROUP: the fields of each element; they may reference earlier members and enclosing fields
};

struct Block
{
    static constexpr std::int64_t stored = -1, compact = -2, to_end = -3; // RECCNT = OFF, CNTR, EOF

    std::vector<Field> fields;
    std::int64_t record_count = stored; // RECCNT: a UINT or compact-index count in the file, no count (records run to the end), or a fixed count
    std::string counted_by;             // RECCNT = <field>: records run to the end; the stored count is how many have <field> nonzero
};

struct Schema : Block
{
    std::vector<Block> blocks;    // further blocks, read in order after the first; only the last may run to the end
    std::string separator = "\t"; // FS; disassembly only, assembly always expects tabs
    bool header = true;           // HEADER: each block's text starts with its column names; further blocks have a preceding \block line; required for several blocks
    std::size_t magic = 0;        // MAGIC: zero dwords before the SafePackage trailer
    bool trailer = true;          // TRAILER: the file ends with the SafePackage marker
    bool mtx_counts = true;       // MTXCNT_OUT; disassembly only
    bool mat_counts = true;       // MATCNT_OUT; disassembly only
    bool ignore_order = false;    // ORD_IGNORE; disassembly only
};

enum class FloatFormat
{
    Shortest,
    Fixed,     // eight decimals; lossy for small values
    Scientific // eight-decimal mantissa, three-digit exponent
};

struct TextOptions
{
    bool legacy = false;                              // byte-oriented text with basic transliteration
    bool force_ascii = false;                         // write every ASCF with an a, hint / encode all ASCF as 8-bit
    std::string charset = "ISO-8859-1";               // single-byte charset for 8-bit ASCF
    FloatFormat float_format = FloatFormat::Shortest; // disassembly only
};

/**
 * @brief Canonical names accepted by TextOptions::charset.
 */
[[nodiscard]] L2ASMDISASM_API std::vector<std::string> supported_charsets();

/**
 * @brief Parse a DDF definition file.
 * @throws std::invalid_argument with line:column on syntax errors or unsupported types/properties
 */
[[nodiscard]] L2ASMDISASM_API Schema parse_ddf(std::string_view definition);

/**
 * @brief Write a schema back as DDF text, e.g. with SOFT* widths updated by disassemble().
 */
[[nodiscard]] L2ASMDISASM_API std::string export_ddf(const Schema &schema);

/**
 * @brief Convert a decrypted DAT payload to text.
 * @param input Decrypted DAT bytes
 * @param sized_schema If set, receives the schema with SOFT* widths grown to fit the data; export it for assemble()
 * @return UTF-8 text, one record per line
 * @throws std::invalid_argument with record, field, and byte offset on malformed input
 */
[[nodiscard]] L2ASMDISASM_API std::string disassemble(std::span<const std::uint8_t> input, const Schema &schema, Schema *sized_schema = nullptr,
                                                      const TextOptions &options = {});
/**
 * @brief Convert tab-separated text back to a decrypted DAT payload.
 * @param schema Must have SOFT* widths at least as large as the data, as exported after disassemble()
 * @throws std::invalid_argument with line and field on malformed text
 */
[[nodiscard]] L2ASMDISASM_API std::vector<std::uint8_t> assemble(std::string_view text, const Schema &schema, const TextOptions &options = {});
} // namespace l2asmdisasm

#endif // L2ASMDISASM_PUBLIC_H
