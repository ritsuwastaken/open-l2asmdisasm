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

struct Name
{
    const Field &field;
    std::size_t element;
    bool indexed;
    const std::string *path = nullptr;

    std::string str() const;
};

using Visit = std::function<Cell(FieldType, const Name &, bool, std::size_t)>;

constexpr std::size_t limit = 100000;

struct Index
{
    std::unordered_map<std::string, std::size_t> first;
    std::vector<bool> primary;
    std::vector<Index> groups;

    Index() = default;
    explicit Index(const std::vector<Field> &fields);
    explicit Index(const Schema &schema) : Index(schema.fields) {}
    std::size_t at(const std::string &name) const;
};

struct Frame
{
    std::vector<Field> &fields;
    const Index &index;
    std::vector<std::int64_t> &values;
    const Frame *parent;
    bool active;
    std::string path;
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
    std::vector<std::size_t> ends{};
    std::size_t columns = 0;

    void record();
    Cell cell(FieldType type, const Name &name, bool active, std::size_t size = 0);
    std::size_t slots(std::size_t &width, std::size_t length, const char *overflow);
    bool enabled(const Frame &frame, const Field &field) const;
    void field(Frame &frame, std::size_t f);
    void group(const Frame &frame, std::size_t f, const Name &element, bool active);
};

void validate(const Schema &schema, const Index &index);
Walker header(Schema &schema, const Index &index, std::vector<std::string> &names);
} // namespace layout

#endif // LAYOUT_H
