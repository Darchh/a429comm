cmake_minimum_required(VERSION 3.1)

project(A429_Communicator CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SOURCES
    a429_main.cpp
)

set(HEADERS
    a429_protocol.h
    a429_communicator.h
)

# Set output directory outside of build
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../bin)

# --- Main Application ---
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# --- Google Test Setup ---
enable_testing()

find_package(Threads REQUIRED)

# Check if googletest directory exists, if not, print error
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/googletest/CMakeLists.txt")
    message(FATAL_ERROR "googletest not found! Please run: git clone https://github.com/google/googletest.git")
endif()
add_subdirectory(googletest)

# --- Test Executable ---
add_executable(a429_test a429_test.cpp ${HEADERS})
target_link_libraries(a429_test gtest gtest_main Threads::Threads)
target_include_directories(a429_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME RunTests COMMAND a429_test)