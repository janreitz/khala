cmake_minimum_required(VERSION 3.20)
project(launcher VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(TBB REQUIRED)

# Platform selection (default to x11)
set(PLATFORM "x11" CACHE STRING "Target platform: x11, wayland, win32")
set_property(CACHE PLATFORM PROPERTY STRINGS x11 wayland win32)

# Platform-specific source and libraries
if(PLATFORM STREQUAL "x11")
    set(WINDOW_SOURCE src/window_x11.cpp)

    find_package(X11 REQUIRED)
    if(NOT X11_FOUND)
        message(FATAL_ERROR "X11 not found")
    endif()

    find_package(X11 COMPONENTS Xrandr)
    if(NOT X11_Xrandr_FOUND)
        message(FATAL_ERROR "XRandR extension not found")
    endif()

    set(PLATFORM_LIBS ${X11_LIBRARIES} ${X11_Xrandr_LIB})
    set(PLATFORM_INCLUDE_DIRS ${X11_INCLUDE_DIR})
    add_compile_definitions(PLATFORM_X11)

elseif(PLATFORM STREQUAL "wayland")
    set(WINDOW_SOURCE src/window_wayland.cpp)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WAYLAND REQUIRED
        wayland-client
        wayland-cursor
        xkbcommon)

    set(PLATFORM_LIBS ${WAYLAND_LIBRARIES})
    set(PLATFORM_INCLUDE_DIRS ${WAYLAND_INCLUDE_DIRS})
    add_compile_definitions(PLATFORM_WAYLAND)

elseif(PLATFORM STREQUAL "win32")
    set(WINDOW_SOURCE src/window_win32.cpp)
    set(PLATFORM_LIBS gdi32)
    set(PLATFORM_INCLUDE_DIRS)
    add_compile_definitions(PLATFORM_WIN32)
else()
    message(FATAL_ERROR "Unknown platform: ${PLATFORM}")
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
    ${WINDOW_SOURCE}
    src/utility.cpp
)

target_compile_definitions(khala PRIVATE
    KHALA_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/khala")

target_include_directories(khala PRIVATE
    ${PLATFORM_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include
    ${CAIRO_INCLUDE_DIRS}
    ${PANGO_INCLUDE_DIRS}
    ${PANGOCAIRO_INCLUDE_DIRS}
)

target_link_libraries(khala PRIVATE
    ${PLATFORM_LIBS}
    ${CAIRO_LIBRARIES}
    ${PANGO_LIBRARIES}
    ${PANGOCAIRO_LIBRARIES}
    TBB::tbb
)

add_executable(indexer_benchmark
    src/indexer_benchmark.cpp
    src/config.cpp
    src/indexer.cpp
    src/ranker.cpp
    src/streamingindex.cpp
    src/utility.cpp
    src/fuzzy.cpp
)

target_compile_definitions(indexer_benchmark PRIVATE
    KHALA_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/khala")

target_link_libraries(indexer_benchmark PRIVATE TBB::tbb)

install(TARGETS khala DESTINATION bin)
install(DIRECTORY commands/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/khala/commands)
install(DIRECTORY themes/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/khala/themes)