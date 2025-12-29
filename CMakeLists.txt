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

# Find PkgConfig for Cairo and Pango
find_package(PkgConfig REQUIRED)

# Find Cairo
pkg_check_modules(CAIRO REQUIRED cairo)

# Find Pango and PangoCairo
pkg_check_modules(PANGO REQUIRED pango)
pkg_check_modules(PANGOCAIRO REQUIRED pangocairo)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_executable(launcher
    src/launcher.cpp
    src/indexer.cpp
    src/utility.cpp
)

target_include_directories(launcher PRIVATE
    ${X11_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CAIRO_INCLUDE_DIRS}
    ${PANGO_INCLUDE_DIRS}
    ${PANGOCAIRO_INCLUDE_DIRS}
)

target_link_libraries(launcher PRIVATE
    ${X11_LIBRARIES}
    ${CAIRO_LIBRARIES}
    ${PANGO_LIBRARIES}
    ${PANGOCAIRO_LIBRARIES}
)

add_executable(indexer_benchmark
    src/indexer_benchmark.cpp
    src/indexer.cpp
    src/utility.cpp
)

install(TARGETS launcher indexer_benchmark DESTINATION bin)