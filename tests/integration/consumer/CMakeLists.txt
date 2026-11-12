# A minimal consumer: fetch cursiv, declare a language, link the module the
# build generates, include no cursiv header but the generated one. Configured
# alone, then built as a directory of another project, by `fetchcontent_test`.
cmake_minimum_required(VERSION 3.30)
project(cursiv_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(cursiv SOURCE_DIR ${CURSIV_SOURCE_DIR})
FetchContent_MakeAvailable(cursiv)

# Read by the grammar's directory, the generator included, cross builds too.
add_compile_definitions(TINY_CONSUMER=1)

cursiv_generate_language(ID         tiny
    GRAMMAR    tiny/TinyParser.hpp
    NAMESPACE  tiny::Tiny
    EXTENSIONS .tiny
    OUT        ${CMAKE_CURRENT_BINARY_DIR}/generated-src/src/tiny/generated)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE tinyGenerated)

# Every benchmark uses the language's LSP module and the same public driver.
add_library(tinyLsp INTERFACE)
target_link_libraries(tinyLsp INTERFACE tinyGenerated cursiv::lsp)
cursiv_add_benchmark(tiny SOURCES bench.cpp)
add_executable(tiny_generated_bench generated_bench.cpp)
target_link_libraries(tiny_generated_bench PRIVATE cursiv::bench tinyLsp)
add_executable(tiny_services_bench services_bench.cpp)
target_link_libraries(tiny_services_bench PRIVATE cursiv::bench tinyLsp)
set_target_properties(tiny_bench tiny_generated_bench tiny_services_bench
    PROPERTIES EXCLUDE_FROM_ALL ON)

enable_testing()
add_test(NAME consumer_reads COMMAND consumer)
add_test(NAME consumer_benchmark
    COMMAND ${CMAKE_COMMAND}
        -DBENCH=$<TARGET_FILE:tiny_bench>
        -DGENERATED=$<TARGET_FILE:tiny_generated_bench>
        -DSERVICES_BENCH=$<TARGET_FILE:tiny_services_bench>
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/benchmark-test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/../benchmark_test.cmake)
