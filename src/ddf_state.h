#ifndef DDF_STATE_H
#define DDF_STATE_H

#include <cstddef>
#include <cstdint>
#include <l2asmdisasm.h>
#include <string>
#include <string_view>
#include <utility>
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
    explicit State(std::string_view definition) : source(definition) {}

    std::string_view source;
    Schema schema;
    std::vector<std::pair<Value, Value>> items;
    Block next;
    std::size_t opened = 0;
    std::vector<Field> groups;

    [[noreturn]] void fail(std::size_t offset, const std::string &message) const;
    [[noreturn]] void unexpected(Token token) const;
    void global(Token name_token, Token value, bool quoted);
    void declare(Token type_token, Token name_token, Token count_token, char suffix);
    void open_group();
    void close_group(Token name_token, Token count_token, bool array);
    void add(Field field, Token name_token, Token count_token, bool array);
    void open_block();
    void property(Token name_token, Token value);
    void list_property(Token name_token);
    Schema finish();

  private:
    std::size_t columns = 0;

    std::string text(Token token) const { return std::string(source.substr(token.offset, token.size)); }

    std::string unquote(Token token) const;
    std::int64_t integer(Token token) const;
    std::size_t count(Token token, const char *message) const;
    const Field *find(const std::string &name, std::size_t limit) const;
    std::vector<Field> &list() { return groups.empty() ? block().fields : groups.back().members; }
    const std::vector<Field> &list() const { return groups.empty() ? block().fields : groups.back().members; }

    Field &current(Token name_token);
    Block &block();
    const Block &block() const;
    double skip_value(const Field &field, Token token) const;
    void skip_if(Field &field, Token name_token);
    void enabled_by(Field &field);
};
} // namespace ddf

#endif // DDF_STATE_H
