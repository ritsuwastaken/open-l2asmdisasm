cmake_minimum_required(VERSION 3.28)
project(l2asmdisasm VERSION 0.1.1 LANGUAGES CXX)

include(FetchContent)
include(GenerateExportHeader)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(L2ASMDISASM_BUILD_CLI "Build the command-line tool" ${PROJECT_IS_TOP_LEVEL})
option(L2ASMDISASM_BUILD_TESTS "Build tests" ${PROJECT_IS_TOP_LEVEL})

add_library(${PROJECT_NAME}
    src/l2asmdisasm.cpp
    src/binary.cpp
    src/dat.cpp
    src/layout.cpp
    src/ddf.cpp
    src/ddf_lexer.cpp
    src/ddf_state.cpp
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

if(L2ASMDISASM_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(L2ASMDISASM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
