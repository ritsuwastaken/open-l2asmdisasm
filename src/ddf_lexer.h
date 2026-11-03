#ifndef DDF_LEXER_H
#define DDF_LEXER_H

#include "ddf_state.h"

namespace ddf
{
enum class Kind
{
    End,
    Word,
    String,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    LParen,
    RParen,
    Equals,
    Comma,
    Semicolon,
    Colon,
    DotDot
};

class Lexer
{
  public:
    explicit Lexer(const State &owner) : state(owner), source(owner.source) { advance(); }

    Kind kind = Kind::End;
    Token token{};

    void advance();

  private:
    const State &state;
    std::string_view source;
    std::size_t cursor = 0;

    void set(Kind k, std::size_t offset, std::size_t size);
};
} // namespace ddf

#endif // DDF_LEXER_H
