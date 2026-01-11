cmake_minimum_required(VERSION 3.20)
project(launcher VERSION 0.1.0 LANGUAGES C CXX)

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
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WAYLAND REQUIRED
        wayland-client
        wayland-cursor
        xkbcommon)

    # Find wayland-scanner
    find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

    # Protocol XML locations
    pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
    if(NOT WAYLAND_PROTOCOLS_DIR)
        set(WAYLAND_PROTOCOLS_DIR /usr/share/wayland-protocols)
    endif()

    # Generate xdg-shell protocol
    set(XDG_SHELL_XML ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml)
    set(XDG_SHELL_CLIENT_H ${CMAKE_BINARY_DIR}/xdg-shell-client-protocol.h)
    set(XDG_SHELL_CLIENT_C ${CMAKE_BINARY_DIR}/xdg-shell-protocol.c)

    add_custom_command(
        OUTPUT ${XDG_SHELL_CLIENT_H}
        COMMAND ${WAYLAND_SCANNER} client-header ${XDG_SHELL_XML} ${XDG_SHELL_CLIENT_H}
        DEPENDS ${XDG_SHELL_XML}
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${XDG_SHELL_CLIENT_C}
        COMMAND ${WAYLAND_SCANNER} private-code ${XDG_SHELL_XML} ${XDG_SHELL_CLIENT_C}
        DEPENDS ${XDG_SHELL_XML}
        VERBATIM
    )

    # Generate xdg-activation protocol
    set(XDG_ACTIVATION_XML ${WAYLAND_PROTOCOLS_DIR}/staging/xdg-activation/xdg-activation-v1.xml)
    set(XDG_ACTIVATION_CLIENT_H ${CMAKE_BINARY_DIR}/xdg-activation-v1-client-protocol.h)
    set(XDG_ACTIVATION_CLIENT_C ${CMAKE_BINARY_DIR}/xdg-activation-v1-protocol.c)

    add_custom_command(
        OUTPUT ${XDG_ACTIVATION_CLIENT_H}
        COMMAND ${WAYLAND_SCANNER} client-header ${XDG_ACTIVATION_XML} ${XDG_ACTIVATION_CLIENT_H}
        DEPENDS ${XDG_ACTIVATION_XML}
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${XDG_ACTIVATION_CLIENT_C}
        COMMAND ${WAYLAND_SCANNER} private-code ${XDG_ACTIVATION_XML} ${XDG_ACTIVATION_CLIENT_C}
        DEPENDS ${XDG_ACTIVATION_XML}
        VERBATIM
    )

    # Create a static library for protocol implementations
    add_library(wayland-protocols STATIC
        ${XDG_SHELL_CLIENT_H}
        ${XDG_SHELL_CLIENT_C}
        ${XDG_ACTIVATION_CLIENT_H}
        ${XDG_ACTIVATION_CLIENT_C}
    )

    # Set source file properties to treat .h files as C headers (don't compile them)
    set_source_files_properties(
        ${XDG_SHELL_CLIENT_H}
        ${XDG_ACTIVATION_CLIENT_H}
        PROPERTIES HEADER_FILE_ONLY TRUE
    )

    target_include_directories(wayland-protocols PUBLIC
        ${WAYLAND_INCLUDE_DIRS}
        ${CMAKE_BINARY_DIR}
    )

    set(WINDOW_SOURCE src/window_wayland.cpp)
    set(PLATFORM_LIBS ${WAYLAND_LIBRARIES} wayland-protocols)
    set(PLATFORM_INCLUDE_DIRS ${WAYLAND_INCLUDE_DIRS} ${CMAKE_BINARY_DIR})
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

# Optimization flags
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler specific flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Compiler warnings
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wshadow
        -Wnon-virtual-dtor
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wformat=2
        -Wimplicit-fallthrough
        -Wnull-dereference
        -Wdouble-promotion
    )
elseif(MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi /RTC1 /DNOMINMAX")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
    # Compiler warnings
#    add_compile_options(
#        /W4           # High warning level (equivalent to -Wall -Wextra)
#        /WX           # Treat warnings as errors (equivalent to -Werror)
#        /w14242       # Conversion, possible loss of data
#        /w14254       # Larger bit field assigned to smaller bit field
#        /w14263       # Member function does not override any base class virtual member function
#        /w14265       # Class has virtual functions, but destructor is not virtual
#        /w14287       # Unsigned/negative constant mismatch
#        /we4289       # Loop control variable declared in the for-loop is used outside the for-loop scope
#        /w14296       # Expression is always true/false
#        /w14311       # Pointer truncation from one type to another
#        /w14545       # Expression before comma evaluates to a function which is missing an argument list
#        /w14546       # Function call before comma missing argument list
#        /w14547       # Operator before comma has no effect
#        /w14549       # Operator before comma has no effect
#        /w14555       # Expression has no effect
#        /w14619       # #pragma warning: there is no warning number 'number'
#        /w14640       # Thread unsafe static member initialization
#        /w14826       # Conversion from one type to another is sign-extended
#        /w14905       # Wide string literal cast to 'LPSTR'
#        /w14906       # String literal cast to 'LPWSTR'
#        /w14928       # Illegal copy-initialization
#        /permissive-  # Standards conformance mode
#    )
endif()

add_executable(khala
    src/launcher.cpp
    src/logger.cpp
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
    src/logger.cpp
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