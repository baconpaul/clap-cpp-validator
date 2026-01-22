cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)

# CLAP supports standard C; but this validator uses std::filesystem so requires C++ 17
# and macOS 10.15
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.15 CACHE STRING "Minimum macOS version")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(clap-cpp-validator VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(USE_ASAN "Use ASAN builds on gcc/clang mac/lin" OFF)

# Add CLAP library
add_subdirectory(libs/clap EXCLUDE_FROM_ALL)

# Main executable
add_executable(clap-validator
    src/main.cpp
    src/validator.cpp
    src/validator.h
    src/plugin/library.cpp
    src/plugin/library.h
    src/plugin/host.cpp
    src/plugin/host.h
    src/plugin/instance.cpp
    src/plugin/instance.h
    src/tests/test_case.cpp
    src/tests/test_case.h
    src/tests/plugin_library_tests.cpp
    src/tests/plugin_library_tests.h
    src/tests/plugin_tests.cpp
    src/tests/plugin_tests.h
    src/commands/list.cpp
    src/commands/list.h
    src/commands/validate.cpp
    src/commands/validate.h
    src/util.cpp
    src/util.h
)

target_link_libraries(clap-validator PRIVATE clap)
target_include_directories(clap-validator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Platform-specific linking
if(APPLE)
    target_link_libraries(clap-validator PRIVATE "-framework CoreFoundation")
elseif(UNIX)
    target_link_libraries(clap-validator PRIVATE dl pthread)
elseif(WIN32)
    # Windows doesn't need extra libraries for dynamic loading
endif()



if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    if (${USE_ASAN})
        message(STATUS "Sanitizer is ON")
    endif ()

    add_compile_options(
            $<$<BOOL:${USE_ASAN}>:-fsanitize=address>
            $<$<BOOL:${USE_ASAN}>:-fsanitize=undefined>
    )

    add_link_options(
            $<$<BOOL:${USE_ASAN}>:-fsanitize=address>
            $<$<BOOL:${USE_ASAN}>:-fsanitize=undefined>
    )
endif ()
