add_subdirectory(config)

install(PROGRAMS extract_metacg DESTINATION bin)
# generate cgcollector2 compiler/linker wrapper scripts
if(METACG_BUILD_GRAPH_TOOLS)
  function(
    configure_cgcollector2_wrapper_and_install
    TEMPLATE
    OUTPUT_BASENAME
  )
    set(BUILD_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_BASENAME}")
    set(INSTALL_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_BASENAME}.install")

    # build-time values for development
    set(CGCOLLECTOR2_BIN "${CMAKE_BINARY_DIR}/tools/cgcollector2/cgcollector2")
    set(CGCOLLECTOR2_CGMERGE2_BIN "${CMAKE_BINARY_DIR}/tools/cgmerge2/cgmerge2")

    configure_file(
      "${TEMPLATE}"
      "${BUILD_OUTPUT}"
      @ONLY
    )

    # install-time values
    set(CGCOLLECTOR2_BIN "${CMAKE_INSTALL_PREFIX}/bin/cgcollector2")
    set(CGCOLLECTOR2_CGMERGE2_BIN "${CMAKE_INSTALL_PREFIX}/bin/cgmerge2")

    configure_file(
      "${TEMPLATE}"
      "${INSTALL_OUTPUT}"
      @ONLY
    )

    install(
      PROGRAMS "${INSTALL_OUTPUT}"
      DESTINATION "bin"
      RENAME "${OUTPUT_BASENAME}"
    )
  endfunction()

  configure_cgcollector2_wrapper_and_install(
    "${CMAKE_CURRENT_SOURCE_DIR}/cgcollector2_cc_wrapper.sh.in"
    "cgcollector2_cc_wrapper.sh"
  )

  configure_cgcollector2_wrapper_and_install(
    "${CMAKE_CURRENT_SOURCE_DIR}/cgcollector2_cxx_wrapper.sh.in"
    "cgcollector2_cxx_wrapper.sh"
  )

  configure_cgcollector2_wrapper_and_install(
    "${CMAKE_CURRENT_SOURCE_DIR}/cgcollector2_link_wrapper.sh.in"
    "cgcollector2_link_wrapper.sh"
  )
endif()
