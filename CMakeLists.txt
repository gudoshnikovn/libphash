cmake_minimum_required(VERSION 3.10)
project(libphash VERSION 1.2.0 LANGUAGES C)

# --- Options ---
option(PHASH_BUILD_TESTS "Build tests" ON)
option(PHASH_BUILD_SHARED "Build shared library" OFF)

# --- Compiler Flags ---
if(MSVC)
    add_compile_options(/W3 /O2)
else()
    add_compile_options(-Wall -Wextra -O3 -fPIC)
    # Architecture specific optimizations
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
        add_compile_options(-msse4.2)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        add_compile_options(-march=armv8-a+simd)
    endif()
endif()

# --- Library ---
file(GLOB_RECURSE SOURCES "src/*.c")

if(PHASH_BUILD_SHARED)
    add_library(phash SHARED ${SOURCES})
    target_compile_definitions(phash PRIVATE LIBPHASH_EXPORTS)
else()
    add_library(phash STATIC ${SOURCES})
endif()

target_include_directories(phash PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(phash PRIVATE m) # Link math library

# --- Tests ---
if(PHASH_BUILD_TESTS)
    enable_testing()
    file(GLOB TEST_SOURCES "tests/test_*.c")
    foreach(test_src ${TEST_SOURCES})
        get_filename_component(test_name ${test_src} NAME_WE)
        add_executable(${test_name} ${test_src})
        target_link_libraries(${test_name} PRIVATE phash)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
