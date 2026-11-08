get_target_property(CPACK_PACKAGE_NAME l2asmdisasm NAME)
get_target_property(CPACK_PACKAGE_VERSION l2asmdisasm VERSION)

add_executable(l2asmdisasm_cli cli.cpp)
target_compile_features(l2asmdisasm_cli PRIVATE cxx_std_20)
target_link_libraries(l2asmdisasm_cli PRIVATE l2asmdisasm::l2asmdisasm)
set_target_properties(l2asmdisasm_cli PROPERTIES OUTPUT_NAME ${CPACK_PACKAGE_NAME} PDB_NAME l2asmdisasm_cli)

install(TARGETS l2asmdisasm_cli DESTINATION .)
install(FILES ${PROJECT_SOURCE_DIR}/JSON.md README.md DESTINATION .)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/definitions" DESTINATION .)

include(CPack)
