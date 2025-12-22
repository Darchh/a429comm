cmake_minimum_required(VERSION 3.1)

project(A429_Communicator CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LIB_SOURCES
    a429_communicator.cpp
    a429_udp.cpp
)

set(HEADERS
    a429_protocol.h
    a429_communicator.h
    a429_udp.h
    a429_export.h
)

# Set output directory outside of build
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../bin)

# --- Library ---
# Create a library from the core source files.
# This can be built as SHARED or STATIC by setting -DBUILD_SHARED_LIBS=ON/OFF
add_library(a429_lib SHARED ${LIB_SOURCES})

# Define A429_BUILD_DLL for Windows symbol export/import
target_compile_definitions(a429_lib PRIVATE A429_BUILD_DLL)

# Set symbol visibility for GCC/Clang to export only marked symbols
set_target_properties(a429_lib PROPERTIES CXX_VISIBILITY_PRESET hidden)

target_include_directories(a429_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# --- Main Application ---
add_executable(${PROJECT_NAME} a429_main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE a429_lib)

# --- Google Test Setup ---
enable_testing()

find_package(Threads REQUIRED)

# Check if googletest directory exists, if not, print error
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/googletest/CMakeLists.txt")
    message(FATAL_ERROR "googletest not found! Please run: git clone https://github.com/google/googletest.git")
endif()
add_subdirectory(googletest)

# --- Test Executable ---
add_executable(a429_test a429_test.cpp)
# Link the test executable against our library and Google Test
target_link_libraries(a429_test PRIVATE a429_lib gtest Threads::Threads)

add_test(NAME RunTests COMMAND a429_test)