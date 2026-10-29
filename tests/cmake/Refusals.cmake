if(NOT CMAKE_CROSSCOMPILING)

  set(_refusal_libraries cursivCore cursivGen cursivLrGen cursivLlGen)
  if(TARGET cursivAllocator)
    list(APPEND _refusal_libraries cursivAllocator)
  endif()
  add_custom_target(refusal_dependencies ALL DEPENDS ${_refusal_libraries})
  export(TARGETS ${_refusal_libraries}
      FILE ${CMAKE_CURRENT_BINARY_DIR}/refusals/engines.cmake)

  get_directory_property(_refusal_options COMPILE_OPTIONS)
  get_directory_property(_refusal_definitions COMPILE_DEFINITIONS)
  string(CONCAT _refusal_settings
      "set(CURSIV_REFUSAL_ROOT [==[${CURSIV_SOURCE_ROOT}]==])\n"
      "set(CURSIV_REFUSAL_COMPILE_OPTIONS [==[${_refusal_options}]==])\n"
      "set(CURSIV_REFUSAL_COMPILE_DEFINITIONS [==[${_refusal_definitions}]==])\n")
  set(_refusal_variables
      CMAKE_TOOLCHAIN_FILE CMAKE_SYSROOT
      CMAKE_CXX_COMPILER CMAKE_CXX_COMPILER_ARG1
      CMAKE_CXX_COMPILER_TARGET CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN
      CMAKE_CXX_COMPILER_LAUNCHER CMAKE_CXX_LINKER_LAUNCHER
      CMAKE_CXX_STANDARD CMAKE_CXX_STANDARD_REQUIRED CMAKE_CXX_EXTENSIONS
      CMAKE_COMPILE_WARNING_AS_ERROR
      CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS
      CMAKE_OSX_SYSROOT CMAKE_OSX_ARCHITECTURES CMAKE_OSX_DEPLOYMENT_TARGET)
  foreach(_config IN ITEMS Debug Release RelWithDebInfo MinSizeRel
                          ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    string(TOUPPER "${_config}" _config)
    list(APPEND _refusal_variables CMAKE_CXX_FLAGS_${_config}
                                  CMAKE_EXE_LINKER_FLAGS_${_config})
  endforeach()
  list(REMOVE_DUPLICATES _refusal_variables)
  foreach(_variable IN LISTS _refusal_variables)
    if(DEFINED ${_variable})
      string(APPEND _refusal_settings
          "set(${_variable} [==[${${_variable}}]==])\n")
    endif()
  endforeach()
  file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/refusals/settings.cmake
      CONTENT "${_refusal_settings}" @ONLY)

  # Each fixture names its lanes, one line per expected diagnostic:
  # `// expect LR LL: <regex>`. Grammar type checks and lexical validation are
  # shared by both backends, so they run on LR alone; backend lowering
  # contracts name LR LL.
  file(GLOB _refusal_cases CONFIGURE_DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/refusals/*.hpp)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      ${_refusal_cases})
  foreach(_file IN LISTS _refusal_cases)
    get_filename_component(_case ${_file} NAME_WE)
    file(STRINGS ${_file} _expectations ENCODING UTF-8
        REGEX "^// expect [A-Z ]+: ")
    foreach(_expectation IN LISTS _expectations)
      string(REGEX MATCH "^// expect ([A-Z ]+): (.*)$" _ "${_expectation}")
      set(_expected "${CMAKE_MATCH_2}")
      string(REPLACE " " ";" _engines "${CMAKE_MATCH_1}")
      foreach(_engine IN LISTS _engines)
        set(_lane ${_case})
        if(_engine STREQUAL "LL")
          set(_lane ${_case}_ll)
        endif()
        add_test(NAME refusal_${_lane} COMMAND ${CMAKE_COMMAND}
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/refusals"
            "-DPARENT=${CMAKE_CURRENT_BINARY_DIR}"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/refusals/${_lane}"
            "-DCASE=${_case}"
            "-DLANE=${_lane}"
            "-DENGINE=${_engine}"
            "-DCONFIG=$<CONFIG>"
            "-DBUILD_GENERATOR=${CMAKE_GENERATOR}"
            "-DEXPECTED=${_expected}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/refusals/run.cmake)
      endforeach()
    endforeach()
  endforeach()
endif()
