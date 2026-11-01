#include "ddf.h"
#include "ddf_grammar.h"
#include "util.h"
#include <algorithm>
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace ddf
{
using namespace l2asmdisasm;

struct Token
{
    std::size_t offset, size;
};

struct Value
{
    Token first, second;
    char op;
};

class State
{
  public:
    explicit State(std::string_view source) : source(source) {}

    std::string_view source;
    Schema schema;
    std::vector<std::pair<Value, Value>> items;
    bool closed = false;

    [[noreturn]] void fail(std::size_t offset, const std::string &message) const
    {
        const auto before = source.substr(0, offset);
        const auto line = std::count(before.begin(), before.end(), '\n') + 1;
        const auto column = offset - (before.rfind('\n') == std::string_view::npos ? 0 : before.rfind('\n') + 1) + 1;
        throw std::invalid_argument("DDF " + std::to_string(line) + ":" + std::to_string(column) + ": " + message);
    }

    [[noreturn]] void syntax_error(int major, Token token) const
    {
        if (closed) fail(token.offset, "Unexpected content after schema");
        if (major == 0) fail(token.offset, "Unexpected end of input");
        fail(token.offset, "Unexpected '" + text(token) + "'");
    }

    void global(Token name_token, Token value, bool quoted)
    {
        const auto name = text(name_token);
        if (name == "FS")
        {
            if (!quoted) fail(value.offset, "FS requires a quoted string");
            schema.separator = unquote(value);
            if (schema.separator.empty()) fail(value.offset, "FS must be a non-empty separator");
            return;
        }
        if (quoted) fail(value.offset, "Expected integer for '" + name + "'");
        const auto number = integer(value);
        if (name == "RECCNT")
        {
            if (number < -1 || number > std::numeric_limits<std::uint32_t>::max()) fail(value.offset, "Invalid RECCNT");
            schema.record_count = number;
        }
        else if (name == "MAGIC")
        {
            if (number < 0 || number > 100000) fail(value.offset, "Invalid MAGIC");
            schema.magic = static_cast<std::size_t>(number);
        }
        else if (name == "HEADER" || name == "ORD_IGNORE" || name == "MTXCNT_OUT" || name == "MATCNT_OUT")
        {
            if (number != 0 && number != 1) fail(value.offset, "Expected YES or NO for '" + name + "'");
            (name == "HEADER"       ? schema.header
             : name == "MTXCNT_OUT" ? schema.mtx_counts
             : name == "MATCNT_OUT" ? schema.mat_counts
                                    : schema.ignore_order) = number != 0;
        }
        else fail(name_token.offset, "Unsupported global '" + name + "'");
    }

    void declare(Token type_token, Token name_token, Token count_token, char suffix)
    {
        const auto type = util::field_type(text(type_token));
        if (!type) fail(type_token.offset, "Unsupported field type or property '" + text(type_token) + "'");
        Field field{*type, text(name_token)};
        if (std::isdigit(static_cast<unsigned char>(field.name.front()))) fail(name_token.offset, "Invalid field name '" + field.name + "'");
        if ((field.type == FieldType::FILLER) != (suffix == '{'))
            fail(name_token.offset, field.type == FieldType::FILLER ? "FILLER requires a {size}" : "Only FILLER takes a {size}");
        if (suffix == '{')
        {
            field.count = count(count_token, "FILLER size must be in [1, 100000]");
        }
        else if (suffix == '[')
        {
            const auto reference = text(count_token);
            if (const auto prior = find(reference, schema.fields.size()))
            {
                if (!util::is_integer(prior->type)) fail(count_token.offset, "Array counter must reference a preceding integer field");
                field.counter = reference;
            }
            else field.count = count(count_token, "Fixed array count must be in [1, 100000]");
            field.array = true;
        }
        columns += field.type == FieldType::FILLER ? 1 : field.count;
        if (columns > 100000) fail(name_token.offset, "Schema exceeds 100000 columns");
        schema.fields.push_back(std::move(field));
    }

    void property(Token name_token, Token value)
    {
        auto &field = current(name_token);
        const auto name = text(name_token);
        if (name == "ORD")
        {
            const auto number = integer(value);
            if (field.order || number < -2147483648LL || number > 2147483647LL) fail(value.offset, "Invalid or duplicate ORD");
            field.order = static_cast<std::int32_t>(number);
            return;
        }
        if (name == "SKIPIF" || name == "ENBBY") fail(value.offset, "Expected '[' after " + name);
        if (name != "SOFT" && name != "SOFTM" && name != "SOFTT") fail(name_token.offset, "Unsupported field type or property '" + name + "'");
        if ((name == "SOFT" && field.counter.empty()) ||
            (name != "SOFT" && field.type < FieldType::MTX) ||
            (name == "SOFTT" && field.type >= FieldType::MAT))
            fail(name_token.offset, "Inapplicable property '" + name + "'");
        const auto number = integer(value);
        if (number < 0 || number > 100000) fail(value.offset, "Invalid SOFT width");
        (name == "SOFT" ? field.soft : name == "SOFTM" ? field.softm
                                                       : field.softt) = static_cast<std::size_t>(number);
    }

    void list_property(Token name_token)
    {
        auto &field = current(name_token);
        const auto name = text(name_token);
        if (name == "SKIPIF") skip_if(field, name_token);
        else if (name == "ENBBY") enabled_by(field);
        else fail(name_token.offset, "Unsupported field type or property '" + name + "'");
        items.clear();
    }

    Schema finish()
    {
        if (schema.fields.empty()) fail(source.size(), "Schema must contain at least one field");
        return std::move(schema);
    }

  private:
    std::size_t columns = 0;

    std::string text(Token token) const { return std::string(source.substr(token.offset, token.size)); }

    std::string unquote(Token token) const
    {
        std::string value;
        for (std::size_t i = token.offset + 1; i + 1 < token.offset + token.size; ++i)
        {
            char c = source[i];
            if (c == '\\' && (c = source[++i]) == 't') c = '\t';
            value += c;
        }
        return value;
    }

    std::int64_t integer(Token token) const
    {
        const auto word = text(token);
        if (word == "OFF") return -1;
        if (word == "YES") return 1;
        if (word == "NO") return 0;
        std::int64_t value{};
        const auto result = std::from_chars(word.data(), word.data() + word.size(), value);
        if (result.ec != std::errc{} || result.ptr != word.data() + word.size()) fail(token.offset, "Expected integer, got '" + word + "'");
        return value;
    }

    std::size_t count(Token token, const char *message) const
    {
        const auto value = integer(token);
        if (value <= 0 || value > 100000) fail(token.offset, message);
        return static_cast<std::size_t>(value);
    }

    const Field *find(const std::string &name, std::size_t limit) const
    {
        const auto end = schema.fields.begin() + static_cast<std::ptrdiff_t>(limit);
        const auto found = std::find_if(schema.fields.begin(), end, [&](const Field &f)
                                        { return f.name == name; });
        return found == end ? nullptr : &*found;
    }

    Field &current(Token name_token)
    {
        if (schema.fields.empty()) fail(name_token.offset, "Unsupported field type or property '" + text(name_token) + "'");
        return schema.fields.back();
    }

    double skip_value(const Field &field, Token token) const
    {
        if (field.type != FieldType::FLOAT)
        {
            const auto value = integer(token);
            if (value < -2147483648LL || value > 4294967295LL) fail(token.offset, "SKIPIF integer exceeds 32 bits");
            return static_cast<double>(value);
        }
        const auto word = text(token);
        if (word == "NO" || word == "YES" || word == "OFF") return static_cast<double>(integer(token));
        float value{};
        const auto result = std::from_chars(word.data(), word.data() + word.size(), value);
        if (result.ec != std::errc{} || result.ptr != word.data() + word.size() || !std::isfinite(value)) fail(token.offset, "Invalid SKIPIF FLOAT");
        return value;
    }

    void skip_if(Field &field, Token name_token)
    {
        if (field.type >= FieldType::UNICODE) fail(name_token.offset, "SKIPIF requires an integer or FLOAT field");
        for (const auto &[index, bounds] : items)
        {
            if (index.op == ':' || bounds.op == ':') fail(index.first.offset, "Invalid SKIPIF range");
            const auto first = integer(index.first), last = integer(index.second);
            if (first > last) fail(index.first.offset, "Reversed SKIPIF range");
            if (first < 0 || last > 99999) fail(index.first.offset, "Invalid SKIPIF index range");
            const auto low = skip_value(field, bounds.first), high = skip_value(field, bounds.second);
            if (low > high) fail(bounds.first.offset, "Reversed SKIPIF range");
            field.skip_if.push_back({static_cast<std::size_t>(first), static_cast<std::size_t>(last), low, high});
        }
    }

    void enabled_by(Field &field)
    {
        std::vector<Condition> clause;
        for (const auto &[reference, value] : items)
        {
            if (reference.op == '.' || value.op != 0) fail(reference.first.offset, "Invalid ENBBY condition");
            Condition condition{text(reference.first), integer(value.first)};
            const auto prior = find(condition.field, schema.fields.size() - 1);
            if (!prior || !util::is_integer(prior->type)) fail(reference.first.offset, "ENBBY must reference a preceding integer field");
            if (reference.op == ':') condition.mask = integer(reference.second);
            if (condition.mask < -2147483648LL || condition.mask > 4294967295LL ||
                condition.value < -2147483648LL || condition.value > 4294967295LL) fail(reference.first.offset, "ENBBY value or mask exceeds 32 bits");
            clause.push_back(std::move(condition));
        }
        field.enabled_by.push_back(std::move(clause));
    }
};
} // namespace ddf

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif
#include "ddf_grammar.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace ddf
{
namespace
{
int next(const State &state, const char *begin, const char *&cursor, Token &token)
{
    const char *marker = nullptr;
    for (;;)
    {
        const char *start = cursor;
        token = {static_cast<std::size_t>(start - begin), 0};
        auto emit = [&](int code)
        {
            token.size = static_cast<std::size_t>(cursor - start);
            return code;
        };
        // clang-format off
        /*!re2c
            re2c:define:YYCTYPE = char;
            re2c:define:YYCURSOR = cursor;
            re2c:define:YYMARKER = marker;
            re2c:yyfill:enable = 0;

            stop = [\x00 \t\n\v\f\r[\](){}=,/*\\#:;"];
            wordchar = [^] \ stop;
            word = (wordchar \ [.]) wordchar* | "." ((wordchar \ [.]) wordchar*)?;

            "\x00" {
                if (static_cast<std::size_t>(start - begin) != state.source.size()) state.fail(token.offset, "Unexpected NUL character");
                return 0;
            }
            [ \t\n\v\f\r]+ { continue; }
            ("#" | "//") [^\x00\n]* { continue; }
            "/" "*" {
                const auto end = state.source.find("*" "/", token.offset + 2);
                if (end == std::string_view::npos) state.fail(token.offset, "Unterminated comment");
                cursor = begin + end + 2;
                continue;
            }
            ["] ([^\x00"\\] | [\\][^\x00])* ["] { return emit(TK_STRING); }
            ["] { state.fail(token.offset, "Unterminated quoted string"); }
            "{" { return emit(TK_LBRACE); }
            "}" { return emit(TK_RBRACE); }
            "[" { return emit(TK_LBRACKET); }
            "]" { return emit(TK_RBRACKET); }
            "(" { return emit(TK_LPAREN); }
            ")" { return emit(TK_RPAREN); }
            "=" { return emit(TK_EQ); }
            "," { return emit(TK_COMMA); }
            ";" { return emit(TK_SEMI); }
            ":" { return emit(TK_COLON); }
            ".." { return emit(TK_DOTDOT); }
            word { return emit(TK_WORD); }
            * { state.fail(token.offset, std::string("Unexpected character '") + *start + "'"); }
        */
        // clang-format on
    }
}
} // namespace

Schema parse(std::string_view definition)
{
    const std::string source(definition);
    State state(source);
    yyParser parser;
    DdfParseInit(&parser);
    const char *cursor = source.c_str();
    int depth = 0;
    for (;;)
    {
        Token token{};
        const int code = next(state, source.c_str(), cursor, token);
        DdfParse(&parser, code, token, &state);
        if (code == 0) break;
        depth += code == TK_LBRACE ? 1 : code == TK_RBRACE ? -1
                                                           : 0;
        if (code == TK_RBRACE && depth == 0) state.closed = true;
    }
    DdfParseFinalize(&parser);
    return state.finish();
}
} // namespace ddf
