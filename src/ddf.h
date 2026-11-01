#ifndef DDF_H
#define DDF_H

#include <l2asmdisasm.h>
#include <string>
#include <string_view>

namespace ddf
{
l2asmdisasm::Schema parse(std::string_view definition);
std::string write(const l2asmdisasm::Schema &schema);
} // namespace ddf

#endif // DDF_H
