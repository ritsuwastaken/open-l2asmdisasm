#include "ddf.h"
#include "ddf_lexer.h"

namespace ddf
{
namespace
{
class Parser
{
  public:
    explicit Parser(State &s) : state(s), lexer(s) {}

    void parse()
    {
        do
        {
            globals();
            block();
        } while (lexer.kind != Kind::End);
    }

  private:
    static constexpr int max_depth = 100;

    State &state;
    Lexer lexer;

    Token expect(Kind k)
    {
        if (lexer.kind != k) state.unexpected(lexer.token);
        const auto taken = lexer.token;
        lexer.advance();
        return taken;
    }

    bool accept(Kind k)
    {
        if (lexer.kind != k) return false;
        lexer.advance();
        return true;
    }

    void globals()
    {
        while (lexer.kind == Kind::Word)
        {
            const auto name = expect(Kind::Word);
            expect(Kind::Equals);
            const bool quoted = lexer.kind == Kind::String;
            const auto value = quoted ? expect(Kind::String) : expect(Kind::Word);
            expect(Kind::Semicolon);
            state.global(name, value, quoted);
        }
    }

    void block()
    {
        expect(Kind::LBrace);
        state.open_block();
        fields(0);
        expect(Kind::RBrace);
    }

    void fields(int depth)
    {
        while (lexer.kind != Kind::RBrace)
        {
            if (lexer.kind == Kind::LBrace)
            {
                group(depth);
                continue;
            }
            const auto first = expect(Kind::Word);
            if (accept(Kind::Equals)) property(first);
            else declaration(first);
        }
    }

    void group(int depth)
    {
        if (depth >= max_depth) state.fail(lexer.token.offset, "DDF nesting too deep");
        lexer.advance();
        state.open_group();
        fields(depth + 1);
        expect(Kind::RBrace);
        const auto name = expect(Kind::Word);
        Token count{};
        const bool array = accept(Kind::LBracket);
        if (array)
        {
            count = expect(Kind::Word);
            expect(Kind::RBracket);
        }
        expect(Kind::Semicolon);
        state.close_group(name, count, array);
    }

    void property(Token name)
    {
        if (accept(Kind::LBracket))
        {
            do
                item();
            while (accept(Kind::Comma));
            expect(Kind::RBracket);
            expect(Kind::Semicolon);
            state.list_property(name);
            return;
        }
        const auto value = expect(Kind::Word);
        expect(Kind::Semicolon);
        state.property(name, value);
    }

    void declaration(Token type)
    {
        const auto name = expect(Kind::Word);
        Token count{};
        char suffix = 0;
        if (lexer.kind == Kind::LBracket || lexer.kind == Kind::LBrace)
        {
            const bool brace = lexer.kind == Kind::LBrace;
            suffix = brace ? '{' : '[';
            lexer.advance();
            count = expect(Kind::Word);
            expect(brace ? Kind::RBrace : Kind::RBracket);
        }
        expect(Kind::Semicolon);
        state.declare(type, name, count, suffix);
    }

    void item()
    {
        expect(Kind::LParen);
        const auto a = value();
        expect(Kind::Comma);
        const auto b = value();
        expect(Kind::RParen);
        state.items.push_back({a, b});
    }

    Value value()
    {
        const auto first = expect(Kind::Word);
        if (accept(Kind::DotDot)) return {first, expect(Kind::Word), '.'};
        if (accept(Kind::Colon)) return {first, expect(Kind::Word), ':'};
        return {first, first, 0};
    }
};
} // namespace

Schema parse(std::string_view definition)
{
    State state(definition);
    Parser(state).parse();
    return state.finish();
}
} // namespace ddf
