cmake_minimum_required(VERSION 3.30)
project(cursiv_allocator_consumer LANGUAGES CXX)

# Reuse the built core and populated dependency; only the allocator and these
# tiny consumers need compiling in this independent project.
add_library(cursivCore STATIC IMPORTED)
set_target_properties(cursivCore PROPERTIES
    IMPORTED_LOCATION "${CORE}"
    INTERFACE_INCLUDE_DIRECTORIES "${CURSIV}/src"
    INTERFACE_COMPILE_FEATURES cxx_std_20)
set(FETCHCONTENT_SOURCE_DIR_CURSIV_MIMALLOC "${MIMALLOC_SOURCE}")
include("${CURSIV}/cmake/CursivAllocator.cmake")

enable_testing()
foreach(_name IN ITEMS normal sanitized)
  add_executable(${_name} main.cpp)
  target_link_libraries(${_name} PRIVATE cursivCore)
  add_test(NAME ${_name} COMMAND ${_name})
endforeach()

if(CURSIV_MIMALLOC)
  target_include_directories(normal PRIVATE "${MIMALLOC_SOURCE}/include")
  target_compile_definitions(normal PRIVATE EXPECT_MIMALLOC)
  set_property(TARGET sanitized PROPERTY CURSIV_OWN_ALLOCATOR ON)
endif()
target_compile_options(sanitized PRIVATE -fsanitize=address)
target_link_options(sanitized PRIVATE -fsanitize=address)
