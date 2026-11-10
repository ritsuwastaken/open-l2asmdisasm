#ifndef TESTS_SUPPORT_H
#define TESTS_SUPPORT_H

#include <gtest/gtest.h>
#include <l2asmdisasm.h>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace l2asmdisasm
{
inline std::string disassemble(std::span<const std::uint8_t> input, const Schema &schema, const NameTable *names = nullptr)
{
    std::ostringstream output;
    disassemble(input, schema, output, names);
    return std::move(output).str();
}

inline std::vector<std::uint8_t> assemble(std::string_view json, const Schema &schema, NameTable *names = nullptr)
{
    std::ostringstream output;
    assemble(json, schema, output, names);
    const auto bytes = std::move(output).str();
    return {bytes.begin(), bytes.end()};
}
} // namespace l2asmdisasm

template <typename Function>
testing::AssertionResult rejects(Function function, std::string_view expected)
{
    try
    {
        function();
    }
    catch (const std::invalid_argument &error)
    {
        if (std::string_view(error.what()).find(expected) != std::string_view::npos) return testing::AssertionSuccess();
        return testing::AssertionFailure() << "message: " << error.what();
    }
    return testing::AssertionFailure() << "no exception";
}

#define EXPECT_REJECTS(statement, expected) EXPECT_TRUE(rejects([&] { (void)(statement); }, \
                                                                expected))

#define EXPECT_ROUNDTRIP(schema, text)                                                              \
    do                                                                                              \
    {                                                                                               \
        const auto bytes_ = l2asmdisasm::assemble(text, schema);                                    \
        EXPECT_EQ(l2asmdisasm::assemble(l2asmdisasm::disassemble(bytes_, schema), schema), bytes_); \
    } while (0)

#endif // TESTS_SUPPORT_H
