set(PROJECT_NAME CGProcessor)
set(TARGETS_EXPORT_NAME ${PROJECT_NAME}-target)

add_executable(cgproc src/CGProc.cpp)

add_config_include(cgproc)
add_metacg(cgproc)
add_cxxopts(cgproc)
# Provides FunctionSignatureMetadata and AllAliasMetadata (and pulls in the clang includes they need)
target_link_libraries(cgproc PRIVATE cgcollector2-metadata)

install(
  TARGETS cgproc
  EXPORT ${TARGETS_EXPORT_NAME}
  RUNTIME DESTINATION bin
)
