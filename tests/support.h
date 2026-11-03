#ifndef TESTS_SUPPORT_H
#define TESTS_SUPPORT_H

#include <gtest/gtest.h>
#include <l2asmdisasm.h>
#include <stdexcept>
#include <string_view>

inline l2asmdisasm::Schema tsv_schema(std::string_view definition)
{
    auto schema = l2asmdisasm::parse_schema(definition);
    schema.header = false;
    return schema;
}

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

#define EXPECT_REJECTS(statement, expected) EXPECT_TRUE(rejects([&] { \
    (void)(statement);                                                \
},                                                                    \
                                                                expected))

#define EXPECT_ROUNDTRIP(schema, text)                                                                      \
    do                                                                                                      \
    {                                                                                                       \
        const auto bytes_ = l2asmdisasm::assemble_tsv(text, schema);                                        \
        EXPECT_EQ(l2asmdisasm::assemble_tsv(l2asmdisasm::disassemble_tsv(bytes_, schema), schema), bytes_); \
    } while (0)

#endif // TESTS_SUPPORT_H
