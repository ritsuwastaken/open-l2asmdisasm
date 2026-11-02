cmake_minimum_required(VERSION 3.28)
project(l2asmdisasm VERSION 0.1.0 LANGUAGES C CXX)

include(FetchContent)
include(GenerateExportHeader)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(L2ASMDISASM_BUILD_CLI "Build the command-line tool" ${PROJECT_IS_TOP_LEVEL})
option(L2ASMDISASM_BUILD_TESTS "Build tests" ${PROJECT_IS_TOP_LEVEL})

fetchcontent_declare(
    sqlite
    GIT_REPOSITORY https://github.com/sqlite/sqlite.git
    GIT_TAG version-3.54.1
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)
fetchcontent_makeavailable(sqlite)
set(lemon_source "${sqlite_SOURCE_DIR}/tool/lemon.c")
set(lemon_template "${sqlite_SOURCE_DIR}/tool/lempar.c")

add_library(${PROJECT_NAME}
    src/l2asmdisasm.cpp
    src/binary.cpp
    src/dat.cpp
    src/layout.cpp
    src/ddf_export.cpp
    src/encoding.cpp
    src/value.cpp
)
add_library(l2asmdisasm::l2asmdisasm ALIAS l2asmdisasm)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
generate_export_header(${PROJECT_NAME}
    EXPORT_FILE_NAME include/l2asmdisasm-export.h
    EXPORT_MACRO_NAME L2ASMDISASM_API
)
target_include_directories(${PROJECT_NAME}
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_executable(l2asmdisasm_lemon "${lemon_source}")
target_compile_definitions(l2asmdisasm_lemon PRIVATE $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
add_custom_command(
    OUTPUT "${generated}/ddf_grammar.c" "${generated}/ddf_grammar.h"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${generated}"
    COMMAND l2asmdisasm_lemon -q "-T${lemon_template}" "-d${generated}" "${CMAKE_CURRENT_SOURCE_DIR}/src/ddf_grammar.y"
    DEPENDS src/ddf_grammar.y "${lemon_template}" l2asmdisasm_lemon
    VERBATIM
)

find_program(RE2C_EXECUTABLE re2c REQUIRED)
add_custom_command(
    OUTPUT "${generated}/ddf.cpp"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${generated}"
    COMMAND "${RE2C_EXECUTABLE}" -W "${CMAKE_CURRENT_SOURCE_DIR}/src/ddf.re" -o "${generated}/ddf.cpp"
    DEPENDS src/ddf.re
    VERBATIM
)

target_sources(${PROJECT_NAME} PRIVATE "${generated}/ddf.cpp" "${generated}/ddf_grammar.c" "${generated}/ddf_grammar.h")
set_source_files_properties("${generated}/ddf_grammar.c" PROPERTIES HEADER_FILE_ONLY TRUE)
target_include_directories(${PROJECT_NAME} PRIVATE "${generated}")

if(L2ASMDISASM_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(L2ASMDISASM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
