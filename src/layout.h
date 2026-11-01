#ifndef LAYOUT_H
#define LAYOUT_H

#include "value.h"
#include <functional>
#include <l2asmdisasm.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace layout
{
using namespace l2asmdisasm;
using Cell = std::optional<value::Value>;
using Visit = std::function<Cell(FieldType, const std::string &, bool, std::size_t)>;

constexpr std::size_t limit = 100000;

struct Index
{
    std::unordered_map<std::string, std::size_t> first;
    std::vector<bool> primary;

    explicit Index(const Schema &schema);
    std::size_t at(const std::string &name) const;
};

enum class Pass
{
    Header,
    Scan,
    Layout
};

struct Walker
{
    Schema &schema;
    const Index &index;
    Pass pass;
    Visit visit;
    std::vector<std::int64_t> values{};
    std::vector<std::size_t> ends{}, hidden{};
    std::size_t columns = 0;

    void record();
    Cell cell(FieldType type, const std::string &name, bool active, std::size_t size = 0);
    std::size_t slots(std::size_t &soft, std::size_t length, const char *overflow);
    bool holds(const Condition &condition) const;
    bool enabled(const Field &field) const;
    void field(std::size_t f);
    void compound(Field &field, const std::string &name, bool active);
};

void validate(const Schema &schema, const Index &index);
Walker header(Schema &schema, const Index &index, std::vector<std::string> &names);
} // namespace layout

#endif // LAYOUT_H
