# Static archives with no build-time metadata: two builds of the same sources give the
# same bytes, so a published archive can be rebuilt and checked against its checksum.
# GCC and Clang objects are already reproducible; what differs between builds is the
# modification time ar writes into each member's header (and ranlib into the index).
#
# GNU and LLVM ar take the D modifier (deterministic mode: zero timestamps, uids and
# gids), and their ranlib takes -D. Apple's ar and ranlib have no such option and read
# the ZERO_AR_DATE environment variable instead, so on that toolchain each archive
# command runs under `cmake -E env ZERO_AR_DATE=1`.
#
# MSVC stamps the build time into more than the archive: cl into every object file,
# lib into every archive member, link into every DLL and executable, and NASM (which
# assembles libjpeg-turbo's SIMD code) into its COFF objects. /Brepro makes cl, lib and
# link write a fixed value, NASM's --reproducible a zero. What remains is the path of
# each object file, which cl records in the object itself: two MSVC builds match when
# they are built in the same directory, which is why scripts/package_release.sh builds
# in build/package rather than in a temporary directory.
#
# Included by CMakeLists.txt after project(), and given to the libjpeg-turbo sub-build
# as CMAKE_PROJECT_INCLUDE, which runs it after that project's own project() call.

if(MSVC)
    foreach(_phash_var CMAKE_C_FLAGS CMAKE_STATIC_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS
                       CMAKE_MODULE_LINKER_FLAGS CMAKE_EXE_LINKER_FLAGS)
        if(NOT " ${${_phash_var}} " MATCHES " /Brepro ")
            string(APPEND ${_phash_var} " /Brepro")
        endif()
    endforeach()
    # Read once, when a project enables ASM_NASM (libjpeg-turbo's simd/ directory).
    if(NOT " ${CMAKE_ASM_NASM_FLAGS_INIT} " MATCHES " --reproducible ")
        string(APPEND CMAKE_ASM_NASM_FLAGS_INIT " --reproducible")
    endif()
    return()
endif()

if(NOT CMAKE_AR)
    return()
endif()

execute_process(COMMAND "${CMAKE_AR}" --version
    OUTPUT_VARIABLE _phash_ar_version ERROR_QUIET RESULT_VARIABLE _phash_ar_result)
if(_phash_ar_result EQUAL 0 AND _phash_ar_version MATCHES "GNU|LLVM")
    foreach(_phash_lang C ASM_NASM)
        set(CMAKE_${_phash_lang}_ARCHIVE_CREATE "<CMAKE_AR> qcD <TARGET> <LINK_FLAGS> <OBJECTS>")
        set(CMAKE_${_phash_lang}_ARCHIVE_APPEND "<CMAKE_AR> qD <TARGET> <LINK_FLAGS> <OBJECTS>")
        set(CMAKE_${_phash_lang}_ARCHIVE_FINISH "<CMAKE_RANLIB> -D <TARGET>")
    endforeach()
elseif(APPLE)
    # cmake --install runs ranlib once more on every static library it copies, which
    # rewrites the index with the current time; the variable has to reach that run too.
    # Installed first, so it is set before any library is copied.
    install(CODE "set(ENV{ZERO_AR_DATE} 1)")
    foreach(_phash_rule CREATE APPEND FINISH)
        if(DEFINED CMAKE_C_ARCHIVE_${_phash_rule}
           AND NOT CMAKE_C_ARCHIVE_${_phash_rule} MATCHES "ZERO_AR_DATE")
            set(CMAKE_C_ARCHIVE_${_phash_rule}
                "\"${CMAKE_COMMAND}\" -E env ZERO_AR_DATE=1 ${CMAKE_C_ARCHIVE_${_phash_rule}}")
        endif()
    endforeach()
endif()
