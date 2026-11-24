# Installs the libraries, their headers and the `cursiv` CMake package that
# find_package(cursiv CONFIG) reads.
include(CMakePackageConfigHelpers)

set(CURSIV_INSTALL_DATADIR ${CMAKE_INSTALL_DATADIR}/cursiv)
set(_package ${CMAKE_INSTALL_LIBDIR}/cmake/cursiv)
FetchContent_GetProperties(lsp_framework SOURCE_DIR _lsp BINARY_DIR _lsp_build)

# Each library is exported under the alias a project links, and built by default.
set(_targets lsp)
set_target_properties(lsp PROPERTIES EXPORT_NAME lspFramework)
foreach(_name IN ITEMS Core Engine Gen Cli Mcp Lsp Test Bench)
  string(TOLOWER ${_name} _export)
  set_target_properties(cursiv${_name} PROPERTIES EXPORT_NAME ${_export})
  list(APPEND _targets cursiv${_name})
endforeach()
set_target_properties(${_targets} PROPERTIES EXCLUDE_FROM_ALL OFF)
set(_licenses ${PROJECT_SOURCE_DIR}/LICENSE ${_lsp}/LICENSE)
set(_license_names cursiv.LICENSE lsp-framework.LICENSE)
if(TARGET cursivAllocator)
  set_target_properties(cursivAllocator PROPERTIES EXPORT_NAME allocator)
  list(APPEND _targets cursivAllocator)
  list(APPEND _licenses ${CMAKE_CURRENT_LIST_DIR}/patches/mimalloc.LICENSE)
  list(APPEND _license_names mimalloc.LICENSE)
endif()
install(TARGETS ${_targets} EXPORT cursivTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    OBJECTS DESTINATION ${CMAKE_INSTALL_LIBDIR}/cursiv)
install(EXPORT cursivTargets NAMESPACE cursiv:: DESTINATION ${_package})

# Each package keeps its own include root.
foreach(_root IN ITEMS core engine cli mcp lsp test fuzz bench)
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/${_root}/
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cursiv/${_root}
      FILES_MATCHING PATTERN "*.hpp")
endforeach()
install(DIRECTORY ${_lsp}/lsp ${_lsp_build}/generated/lsp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING REGEX "\\.(h|inl)$")

# CursivGrammar.cmake finds the generator's source from its own directory, so
# both keep the paths they have in the sources.
install(FILES ${CMAKE_CURRENT_LIST_DIR}/CursivGrammar.cmake
              ${CMAKE_CURRENT_LIST_DIR}/CursivBenchmark.cmake
    DESTINATION ${CURSIV_INSTALL_DATADIR}/cmake)
cmake_path(RELATIVE_PATH CURSIV_GENERATOR_MAIN BASE_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE _generator)
cmake_path(GET _generator PARENT_PATH _generator)
install(FILES ${CURSIV_GENERATOR_MAIN} DESTINATION ${CURSIV_INSTALL_DATADIR}/${_generator})
foreach(_license _name IN ZIP_LISTS _licenses _license_names)
  install(FILES ${_license} DESTINATION ${CURSIV_INSTALL_DATADIR}/licenses RENAME ${_name})
endforeach()

configure_package_config_file(${CMAKE_CURRENT_LIST_DIR}/cursivConfig.cmake.in
    ${PROJECT_BINARY_DIR}/cursivConfig.cmake
    INSTALL_DESTINATION ${_package}
    PATH_VARS CURSIV_INSTALL_DATADIR)
write_basic_package_version_file(${PROJECT_BINARY_DIR}/cursivConfigVersion.cmake
    COMPATIBILITY SameMinorVersion)
install(FILES ${PROJECT_BINARY_DIR}/cursivConfig.cmake
              ${PROJECT_BINARY_DIR}/cursivConfigVersion.cmake
    DESTINATION ${_package})
