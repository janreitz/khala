cmake_minimum_required(VERSION 3.20)
project(launcher VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(TBB REQUIRED)

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

add_executable(khala
    src/launcher.cpp
    src/actions.cpp
    src/config.cpp
    src/fuzzy.cpp
    src/indexer.cpp
    src/ranker.cpp
    src/streamingindex.cpp
    src/ui.cpp
    src/utility.cpp
)

target_compile_definitions(khala PRIVATE
    KHALA_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/khala")

target_include_directories(khala PRIVATE
    ${X11_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CAIRO_INCLUDE_DIRS}
    ${PANGO_INCLUDE_DIRS}
    ${PANGOCAIRO_INCLUDE_DIRS}
)

target_link_libraries(khala PRIVATE
    ${X11_LIBRARIES}
    ${X11_Xrandr_LIB}
    ${CAIRO_LIBRARIES}
    ${PANGO_LIBRARIES}
    ${PANGOCAIRO_LIBRARIES}
    TBB::tbb
)

add_executable(indexer_benchmark
    src/indexer_benchmark.cpp
    src/indexer.cpp
    src/ranker.cpp
    src/streamingindex.cpp
    src/utility.cpp
    src/fuzzy.cpp
)
target_link_libraries(indexer_benchmark PRIVATE TBB::tbb)

install(TARGETS khala DESTINATION bin)
install(DIRECTORY commands/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/khala/commands)