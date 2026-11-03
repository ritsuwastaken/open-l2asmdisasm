#include "ddf_lexer.h"
#include <cstring>

namespace ddf
{
namespace
{
bool stop(char c) { return std::strchr(" \t\n\v\f\r[](){}=,/*\\#:;\"", c) != nullptr || c == '\0'; }
} // namespace

void Lexer::advance()
{
    for (;;)
    {
        if (cursor >= source.size()) return set(Kind::End, cursor, 0);
        const auto start = cursor;
        const char c = source[cursor];
        if (c == '\0') state.fail(start, "Unexpected NUL character");
        if (std::strchr(" \t\n\v\f\r", c))
        {
            ++cursor;
            continue;
        }
        if (c == '#' || source.substr(cursor, 2) == "//")
        {
            while (cursor < source.size() && source[cursor] != '\n' && source[cursor] != '\0')
                ++cursor;
            continue;
        }
        if (source.substr(cursor, 2) == "/*")
        {
            const auto end = source.find("*/", cursor + 2);
            if (end == std::string_view::npos) state.fail(start, "Unterminated comment");
            cursor = end + 2;
            continue;
        }
        if (c == '"')
        {
            for (++cursor; cursor < source.size() && source[cursor] != '"' && source[cursor] != '\0'; ++cursor)
                if (source[cursor] == '\\' && (++cursor == source.size() || source[cursor] == '\0')) break;
            if (cursor >= source.size() || source[cursor] != '"') state.fail(start, "Unterminated quoted string");
            return set(Kind::String, start, ++cursor - start);
        }
        if (source.substr(cursor, 2) == "..")
        {
            cursor += 2;
            return set(Kind::DotDot, start, 2);
        }
        constexpr std::string_view marks = "{}[]()=,;:";
        if (const auto mark = marks.find(c); mark != std::string_view::npos)
        {
            constexpr Kind kinds[] = {Kind::LBrace, Kind::RBrace, Kind::LBracket, Kind::RBracket, Kind::LParen,
                                      Kind::RParen, Kind::Equals, Kind::Comma, Kind::Semicolon, Kind::Colon};
            return set(kinds[mark], start, ++cursor - start);
        }
        if (stop(c)) state.fail(start, std::string("Unexpected character '") + c + "'");
        ++cursor;
        if (c != '.' || (cursor < source.size() && source[cursor] != '.' && !stop(source[cursor])))
            while (cursor < source.size() && !stop(source[cursor]))
                ++cursor;
        return set(Kind::Word, start, cursor - start);
    }
}

void Lexer::set(Kind k, std::size_t offset, std::size_t size)
{
    kind = k;
    token = {offset, size};
}
} // namespace ddf
