cmake_minimum_required(VERSION 3.20)
project(launcher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find X11
find_package(X11 REQUIRED)

if(NOT X11_FOUND)
    message(FATAL_ERROR "X11 not found!")
endif()

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Main executable
add_executable(launcher
    src/launcher.cpp
)

target_include_directories(launcher PRIVATE
    ${X11_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(launcher PRIVATE
    ${X11_LIBRARIES}
)

# Install target
install(TARGETS launcher DESTINATION bin)
