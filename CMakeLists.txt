cmake_minimum_required(VERSION 3.20)
project(launcher VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenMP)
find_package(TBB)

# Platform selection (default to x11)
set(PLATFORM "x11" CACHE STRING "Target platform: x11, wayland, win32")
set_property(CACHE PLATFORM PROPERTY STRINGS x11 wayland win32)

# Platform-specific source and libraries
if(PLATFORM STREQUAL "x11")
    set(WINDOW_SOURCE src/window_x11.cpp src/window_cairo_impl.cpp)
    set(PLATFORM_UTILITY_SOURCE src/utility_linux.cpp)

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

    set(WINDOW_SOURCE src/window_wayland.cpp src/window_cairo_impl.cpp)
    set(PLATFORM_UTILITY_SOURCE src/utility_linux.cpp)
    set(PLATFORM_LIBS ${WAYLAND_LIBRARIES} wayland-protocols)
    set(PLATFORM_INCLUDE_DIRS ${WAYLAND_INCLUDE_DIRS} ${CMAKE_BINARY_DIR})
    add_compile_definitions(PLATFORM_WAYLAND)

elseif(PLATFORM STREQUAL "win32")
    set(WINDOW_SOURCE src/window_win32.cpp)
    set(PLATFORM_UTILITY_SOURCE src/utility_win32.cpp)
    set(PLATFORM_LIBS gdi32 d2d1 dwrite)
    set(PLATFORM_INCLUDE_DIRS)
    add_compile_definitions(PLATFORM_WIN32)
else()
    message(FATAL_ERROR "Unknown platform: ${PLATFORM}")
endif()

# Cairo/Pango only needed for Linux platforms (X11/Wayland)
if(PLATFORM STREQUAL "x11" OR PLATFORM STREQUAL "wayland")
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(CAIRO REQUIRED cairo)
    pkg_check_modules(PANGO REQUIRED pango)
    pkg_check_modules(PANGOCAIRO REQUIRED pangocairo)
    pkg_check_modules(GOBJECT REQUIRED glib-2.0 gobject-2.0)

    list(APPEND PLATFORM_INCLUDE_DIRS
        ${CAIRO_INCLUDE_DIRS}
        ${PANGO_INCLUDE_DIRS}
        ${PANGOCAIRO_INCLUDE_DIRS}
    )

    list(APPEND PLATFORM_LIBS
        ${CAIRO_LIBRARIES}
        ${PANGO_LIBRARIES}
        ${PANGOCAIRO_LIBRARIES}
        ${GOBJECT_LIBRARIES}
    )
endif()

# Optimization flags
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler specific flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wshadow
        -Wcast-align
        -Wunused
        -Wconversion
        -Wsign-conversion
        -Wformat=2
        -Wimplicit-fallthrough
        -Wnull-dereference
        -Wdouble-promotion
    )

     # C++-only flags
    add_compile_options(
        $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>
        $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>
    )

    # Clang's __cpp_concepts macro is 201907L, but libstdc++ requires >= 202002L
    # for C++23 features like std::expected. We redefine it and suppress the warning.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_definitions(__cpp_concepts=202002L)
        add_compile_options(-Wno-builtin-macro-redefined)
    endif()
elseif(MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi /RTC1 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS")

    add_compile_options(
        /W4          # High warning level (similar to -Wall -Wextra)
        /WX          # Treat warnings as errors (equivalent to -Werror)
        /permissive- # Strict conformance mode
        /w14640      # Thread-unsafe static member initialization
        /w14826      # Conversion is sign-extended
        /w14905      # Wide string literal cast to LPSTR
        /w14906      # String literal cast to LPWSTR
        /w14928      # Illegal copy-initialization
        /w14254      # Operator conversion, possible loss of data
        /w14263      # Function does not override base class virtual
        /w14265      # Class has virtual functions but destructor is not virtual
        /w14287      # Unsigned/negative constant mismatch
        /w14296      # Expression is always false
        /w14311      # Pointer truncation
        /w14545      # Expression before comma evaluates to function
        /w14546      # Function call before comma missing argument list
        /w14547      # Operator before comma has no effect
        /w14549      # Operator before comma has no effect
        /w14555      # Expression has no effect
        /w14619      # Pragma warning: nonexistent warning number
        /w14826      # Conversion sign-extended
        /w14946      # Reinterpret_cast between related types
        /w14242      # Conversion, possible loss of data (like -Wconversion)
        /w14254      # Conversion, possible loss of data
    )
endif()

add_executable(khala
    src/launcher.cpp
    src/logger.cpp
    src/actions.cpp
    src/config.cpp
    src/fuzzy.cpp
    src/indexer.cpp
    src/packed_strings.cpp
    src/ranker.cpp
    src/streamingindex.cpp
    src/ui.cpp
    ${WINDOW_SOURCE}
    ${PLATFORM_UTILITY_SOURCE}
    src/utility.cpp
    src/vector.c
    src/str.cpp
)

if(PLATFORM STREQUAL "win32")
    set_target_properties(khala PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
    target_link_options(khala PRIVATE /ENTRY:mainCRTStartup)
endif()

target_compile_definitions(khala PRIVATE
    KHALA_INSTALL_DIR="${CMAKE_INSTALL_FULL_DATADIR}/khala")

target_include_directories(khala PRIVATE
    ${PLATFORM_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khala PRIVATE
    ${PLATFORM_LIBS}
)

add_executable(indexer_benchmark
    src/indexer_benchmark.cpp
    src/config.cpp
    src/indexer.cpp
    src/logger.cpp
    src/packed_strings.cpp
    src/ranker.cpp
    src/streamingindex.cpp
    src/utility.cpp
    ${PLATFORM_UTILITY_SOURCE}
    src/fuzzy.cpp
    src/vector.c
    src/str.cpp
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(indexer_benchmark PRIVATE OpenMP::OpenMP_CXX)
endif()

if(TBB_FOUND)
    target_link_libraries(indexer_benchmark PRIVATE TBB::tbb)
    target_compile_definitions(indexer_benchmark PRIVATE HAVE_TBB)
endif()

target_compile_definitions(indexer_benchmark PRIVATE
    KHALA_INSTALL_DIR="${CMAKE_INSTALL_FULL_DATADIR}/khala")

install(TARGETS khala DESTINATION bin)
install(DIRECTORY commands/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/khala/commands)
install(DIRECTORY themes/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/khala/themes)