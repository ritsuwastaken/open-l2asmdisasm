#ifndef TESTS_SUPPORT_H
#define TESTS_SUPPORT_H

#include <gtest/gtest.h>
#include <l2asmdisasm.h>
#include <stdexcept>
#include <string_view>

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

#define EXPECT_REJECTS(statement, expected) EXPECT_TRUE(rejects([&] { (void)(statement); }, expected))

#define EXPECT_ROUNDTRIP(schema, text)                                                              \
    do                                                                                              \
    {                                                                                               \
        const auto bytes_ = l2asmdisasm::assemble(text, schema);                                    \
        EXPECT_EQ(l2asmdisasm::assemble(l2asmdisasm::disassemble(bytes_, schema), schema), bytes_); \
    } while (0)

#endif // TESTS_SUPPORT_H
