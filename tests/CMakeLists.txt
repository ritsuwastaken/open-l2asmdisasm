fetchcontent_declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.18.0
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)
fetchcontent_makeavailable(googletest)

add_executable(l2asmdisasm_tests
    test_definitions.cpp
    test_json.cpp
    test_blocks.cpp
    test_compact_index.cpp
    test_dat.cpp
    test_names.cpp
)

target_compile_definitions(l2asmdisasm_tests PRIVATE L2ASMDISASM_DEFINITION_DIR="${PROJECT_SOURCE_DIR}/definitions")
target_compile_options(l2asmdisasm_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(l2asmdisasm_tests PRIVATE GTest::gtest_main l2asmdisasm)

include(GoogleTest)
gtest_add_tests(TARGET l2asmdisasm_tests)
