#ifndef JSON_WRITER_H
#define JSON_WRITER_H

#include "json.h"
#include <ostream>

namespace l2asmdisasm::json
{
class Writer
{
  public:
    Writer(std::ostream &stream, std::size_t blocks);
    void begin_block();
    void record(const Json::Value &record, const RecordPlan &plan);
    void end_block();
    void finish();

  private:
    std::ostream &stream;
    std::string output;
    std::size_t depth, blocks = 0, records = 0;

    void flush();
    void indent(std::size_t level) { output.append(level * 2, ' '); }
    void string(std::string_view text);
    void scalar(const Json::Value &value);
    void write_float(double value, FieldType type);
    void write(const Json::Value &value, const RecordPlan *plan, const Field *field, std::size_t level, bool expand = false);
};
} // namespace l2asmdisasm::json

#endif // JSON_WRITER_H
