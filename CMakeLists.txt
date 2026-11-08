cmake_minimum_required(VERSION 3.28)
project(l2asmdisasm VERSION 0.6.0 LANGUAGES CXX)

include(FetchContent)
include(GenerateExportHeader)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(L2ASMDISASM_BUILD_CLI "Build the command-line tool" ${PROJECT_IS_TOP_LEVEL})
option(L2ASMDISASM_BUILD_TESTS "Build tests" ${PROJECT_IS_TOP_LEVEL})

fetchcontent_declare(jsoncpp
    GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
    GIT_TAG 93acc7b155048f1a66c43235cc205519f1e7a6ea
    EXCLUDE_FROM_ALL
)
block()
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_STATIC_LIBS ON)
    set(BUILD_OBJECT_LIBS OFF)
    set(JSONCPP_WITH_TESTS OFF)
    set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF)
    set(JSONCPP_WITH_CMAKE_PACKAGE OFF)
    set(JSONCPP_WITH_PKGCONFIG_SUPPORT OFF)
    fetchcontent_makeavailable(jsoncpp)
endblock()

set_target_properties(jsoncpp_static PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
add_library(JsonCpp::JsonCpp ALIAS jsoncpp_static)
add_library(${PROJECT_NAME}
    src/json.cpp
    src/json_schema.cpp
    src/json_value.cpp
    src/l2asmdisasm.cpp
    src/binary.cpp
    src/encoding.cpp
)
add_library(l2asmdisasm::l2asmdisasm ALIAS l2asmdisasm)
target_link_libraries(${PROJECT_NAME} PRIVATE JsonCpp::JsonCpp)
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
