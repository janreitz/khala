cmake_minimum_required(VERSION 3.20)
project(launcher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Boost with coroutine and context components
find_package(Boost 1.82 REQUIRED COMPONENTS system filesystem)

# Find SQLite3
find_package(SQLite3 REQUIRED)

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

# Main executable
add_executable(launcher
    src/launcher.cpp
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

# Indexer daemon executable
add_executable(indexer
    src/indexer.cpp
)

target_include_directories(indexer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${Boost_INCLUDE_DIRS}
    ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(indexer PRIVATE
    ${Boost_LIBRARIES}
    ${SQLite3_LIBRARIES}
)

# Install target
install(TARGETS launcher indexer  DESTINATION bin)