include(json)
include(spdlog)
include(cxxopts-lib)

# Internal dependencies
function(add_metacg target)
  add_metacg_library(${target})
  add_graph_includes(${target})
endfunction()

function(add_pgis target)
  add_pgis_includes(${target})
  add_pgis_library(${target})
endfunction()

function(add_config_include target)
  target_include_directories(${target} PUBLIC $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)
endfunction()

function(add_pgis_includes target)
  target_include_directories(
    ${target} PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/pgis/lib/include>
                     $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/pgis/lib/include/config>
  )
endfunction()

function(add_graph_includes target)
  target_include_directories(${target} PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/graph/include>)
endfunction()

function(add_pgis_library target)
  target_link_libraries(${target} PUBLIC pgis)
endfunction()

function(add_metacg_library target)
  target_link_libraries(${target} PUBLIC metacg::metacg)
endfunction()

function(add_metacg_completion target)
  get_property(script_generated GLOBAL PROPERTY METACG_COMPLETION_SCRIPT_GENERATED)
  if(script_generated)
    message(
      FATAL_ERROR
        "add_metacg_completion(${target}) was called after the completion script was generated. "
        "Make sure metacg_generate_completion_script() is called after all targets have been added."
    )
  endif()

  target_link_libraries(${target} PUBLIC MetaCGCompletion)
  # Keep track of all targets with completion support, so the bash completion script can register them
  set_property(GLOBAL APPEND PROPERTY METACG_COMPLETION_TARGETS ${target})
endfunction()

# Generates the bash completion script that registers all targets added via add_metacg_completion(). Needs to be
# called after all targets have been added.
function(metacg_generate_completion_script)
  get_property(completion_targets GLOBAL PROPERTY METACG_COMPLETION_TARGETS)
  set_property(GLOBAL PROPERTY METACG_COMPLETION_SCRIPT_GENERATED TRUE)

  if(NOT completion_targets)
    return()
  endif()

  set(METACG_COMPLETION_REGISTRATIONS "")
  foreach(completion_target IN LISTS completion_targets)
    # Use the name of the produced binary, which may differ from the target name
    get_target_property(binary_name ${completion_target} OUTPUT_NAME)
    if(NOT binary_name)
      set(binary_name ${completion_target})
    endif()
    string(APPEND METACG_COMPLETION_REGISTRATIONS "complete -F _metacg_completion ${binary_name}\n")
  endforeach()

  string(STRIP "${METACG_COMPLETION_REGISTRATIONS}" METACG_COMPLETION_REGISTRATIONS)

  set(completion_script ${PROJECT_BINARY_DIR}/utils/completions/metacg-completion.bash)
  configure_file(${PROJECT_SOURCE_DIR}/utils/completions/metacg-completion.bash.in ${completion_script} @ONLY)

  install(FILES ${completion_script} DESTINATION share/metacg)
endfunction()

# Project compile options
function(target_project_compile_options target)
  cmake_parse_arguments(
    ARG
    ""
    ""
    "PRIVATE_FLAGS;PUBLIC_FLAGS"
    ${ARGN}
  )

  target_compile_options(
    ${target}
    PRIVATE -Wall
            -Wextra
            -pedantic
            -Wunreachable-code
            -Wwrite-strings
            -Wpointer-arith
            -Wcast-align
            -Wcast-qual
            -Wno-unused-parameter
  )

  if(ARG_PRIVATE_FLAGS)
    target_compile_options(${target} PRIVATE "${ARG_PRIVATE_FLAGS}")
  endif()

  if(ARG_PUBLIC_FLAGS)
    target_compile_options(${target} PUBLIC "${ARG_PUBLIC_FLAGS}")
  endif()
endfunction()

# Project compile definitions
function(target_project_compile_definitions target)
  cmake_parse_arguments(
    ARG
    ""
    ""
    "PRIVATE_DEFS;PUBLIC_DEFS"
    ${ARGN}
  )

  target_compile_definitions(${target} PRIVATE LOG_LEVEL=${LOG_LEVEL})

  if(ARG_PRIVATE_DEFS)
    target_compile_definitions(${target} PRIVATE "${ARG_PRIVATE_DEFS}")
  endif()

  if(ARG_PUBLIC_DEFS)
    target_compile_definitions(${target} PUBLIC "${ARG_PUBLIC_DEFS}")
  endif()
endfunction()
