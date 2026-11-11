#ifndef L2ASMDISASM_PUBLIC_H
#define L2ASMDISASM_PUBLIC_H

#ifndef L2ASMDISASM_API
#define L2ASMDISASM_API
#endif

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace l2asmdisasm
{
enum class FieldType
{
    U32,
    I32,
    U16,
    I16,
    U8,
    I8,
    F32,
    UTF16,
    STRING,
    COMPACT,
    PADDING,
    I64,
    F64,
    GROUP
};

struct Condition
{
    std::string field;
    std::int64_t value = 0;
    std::int64_t mask = -1;
};

enum class FieldFormat
{
    Plain,
    IdList, ///< Comma-separated u32 IDs as a JSON array.
    NameRef ///< 32-bit index into L2GameDataName.dat.
};

struct Field
{
    FieldType type;
    std::string name;
    std::size_t count = 1;                          ///< Fixed array length.
    std::size_t size = 0;                           ///< PADDING byte count.
    bool array = false;                             ///< Array field, including length one.
    std::string counter;                            ///< Array length field; a preceding integer in this or a parent scope.
    std::vector<std::vector<Condition>> enabled_by; ///< OR of AND clauses.
    std::vector<Field> members;                     ///< Nested fields for GROUP.
    bool wide = false;                              ///< Default STRING encoding: UTF-16 if true, byte if false.
    std::optional<bool> terminated;                 ///< Trailing null; defaults to nonempty STRING values only.
    FieldFormat format = FieldFormat::Plain;        ///< Value interpretation; see FieldFormat.
};

struct Block
{
    static constexpr std::int64_t stored = -1, compact = -2, eof = -3;

    std::vector<Field> fields;
    std::int64_t record_count = stored; ///< stored (u32), compact, eof, or a fixed nonnegative count.
    std::string nonzero;                ///< Stored count of records with this field nonzero; read to the end.
};

struct Schema
{
    std::vector<std::string> clients;
    std::vector<Block> blocks; ///< Blocks in file order.
    bool safe_package = true;  ///< SafePackage marker at the end.
};

/// L2GameDataName.dat entries in index order.
using NameTable = std::vector<std::string>;

/// Read/write a decrypted L2GameDataName.dat, preserving entry order.
/// @throws std::invalid_argument for malformed tables or invalid UTF-8.
[[nodiscard]] L2ASMDISASM_API NameTable read_name_table(std::span<const std::uint8_t> input);
[[nodiscard]] L2ASMDISASM_API std::vector<std::uint8_t> write_name_table(const NameTable &names);

/// Parse one layout, selecting a client when several exist.
/// @throws std::invalid_argument if the definition or client selection is invalid.
[[nodiscard]] L2ASMDISASM_API Schema parse_schema(std::string_view json, std::string_view client_version = {});
/// Parse all layouts; invalid definitions throw std::invalid_argument.
[[nodiscard]] L2ASMDISASM_API std::vector<Schema> parse_schemas(std::string_view json);

/// Write decrypted DAT bytes to `output` as UTF-8 JSON; byte strings are read as ISO-8859-1.
/// With a name table, name-ref fields export as {"index": n, "value": "..."}; unknown indexes remain numeric.
/// Records are written as they are converted, so on failure `output` may hold an incomplete prefix.
/// @throws std::invalid_argument for invalid definitions, data, or name table; std::runtime_error if `output` fails.
L2ASMDISASM_API void disassemble(std::span<const std::uint8_t> input, const Schema &schema, std::ostream &output,
                                 const NameTable *names = nullptr);
/// Write UTF-8 JSON to `output` as decrypted DAT bytes; open file streams with std::ios::binary.
/// With a name table, unchanged indexes are preserved and edited names are reused or appended; the table changes only on success.
/// Records are written as they are converted, so on failure `output` may hold an incomplete prefix.
/// @throws std::invalid_argument for invalid definitions, data, or name table; std::runtime_error if `output` fails.
L2ASMDISASM_API void assemble(std::string_view json, const Schema &schema, std::ostream &output, NameTable *names = nullptr);
} // namespace l2asmdisasm

#endif // L2ASMDISASM_PUBLIC_H
