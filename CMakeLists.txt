cmake_minimum_required(VERSION 3.15)
project(Tearwash VERSION 0.1.0 LANGUAGES C CXX)

# Tearwash builds standalone (plugin + tests) and as a subdirectory/submodule of another project
# (e.g. Tannhauser). Other plugins that only want the reverb link the `tearwash_engine` target
# (include root: src/, header "tearwash/engine/Reverb.hpp").
#
# Options:
#   TEARWASH_BUILD_PLUGIN   build tearwash.clap and the plugin tests (default: ON only when top-level)
#   TEARWASH_CLAP_INCLUDE_DIR  CLAP headers; default: the clap/ submodule of this repository
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(TEARWASH_TOP_LEVEL ON)
else()
    set(TEARWASH_TOP_LEVEL OFF)
endif()
option(TEARWASH_BUILD_PLUGIN "Build tearwash.clap and its tests" ${TEARWASH_TOP_LEVEL})

if(TEARWASH_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release)
    endif()
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported AND CMAKE_BUILD_TYPE MATCHES "Release")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    endif()
    if(MSVC)
        add_link_options($<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
        add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)
    endif()
endif()

# Reverb engine: no plugin, GUI or CLAP dependencies.
add_library(tearwash_engine STATIC src/tearwash/engine/Reverb.cpp)
target_include_directories(tearwash_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(tearwash_engine PUBLIC cxx_std_17)
set_target_properties(tearwash_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT TEARWASH_BUILD_PLUGIN)
    return()
endif()

if(NOT TEARWASH_CLAP_INCLUDE_DIR)
    set(TEARWASH_CLAP_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/clap/include)
endif()
if(NOT EXISTS ${TEARWASH_CLAP_INCLUDE_DIR}/clap/clap.h)
    message(FATAL_ERROR "CLAP headers not found in ${TEARWASH_CLAP_INCLUDE_DIR}. "
                        "Run: git submodule update --init (or set TEARWASH_CLAP_INCLUDE_DIR).")
endif()

if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    set(TEARWASH_GUI_LIBS ${X11_LIBRARIES})
    set(TEARWASH_X11_INCLUDE ${X11_INCLUDE_DIR})
elseif(APPLE)
    find_library(COCOA_LIBRARY Cocoa)
    set(TEARWASH_GUI_LIBS ${COCOA_LIBRARY})
elseif(WIN32)
    set(TEARWASH_GUI_LIBS gdi32 user32 comdlg32)
endif()

# The Classic drawing code comes from spux (extern/spux, pinned to a commit; SPUX_SOURCE_DIR points at
# a local checkout while developing). Only the plugin needs it: the engine does not.
if(NOT TARGET spux_ui_classic)
    if(SPUX_SOURCE_DIR)
        add_subdirectory(${SPUX_SOURCE_DIR} spux)
    else()
        add_subdirectory(extern/spux spux)
    endif()
endif()

set(TEARWASH_PLUGIN_SOURCES
    src/tearwash/plugin/TwParams.cpp
    src/tearwash/plugin/TwPresets.cpp
    src/tearwash/plugin/TwPresetData.cpp
    src/tearwash/plugin/TearwashClap.cpp
    src/tearwash/plugin/TwGui.cpp
)

# Shared by the plugin and the tests.
function(tearwash_configure target)
    target_include_directories(${target} PRIVATE ${TEARWASH_CLAP_INCLUDE_DIR} ${TEARWASH_X11_INCLUDE})
    target_link_libraries(${target} PRIVATE tearwash_engine spux_ui_classic ${TEARWASH_GUI_LIBS})
endfunction()

add_library(tearwash MODULE ${TEARWASH_PLUGIN_SOURCES})
tearwash_configure(tearwash)
set_target_properties(tearwash PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "tearwash")
if(UNIX AND NOT APPLE)
    # Keep the plugin's symbols to itself; only clap_entry is exported.
    set_target_properties(tearwash PROPERTIES CXX_VISIBILITY_PRESET hidden C_VISIBILITY_PRESET hidden)
endif()

# Copy the built .clap into $CLAPTEST (not part of ALL).
add_custom_target(deploy_tearwash
    COMMAND ${CMAKE_COMMAND} -DCLAP_FILE=$<TARGET_FILE:tearwash>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployClap.cmake
    COMMENT "Deploying tearwash.clap to $CLAPTEST"
    VERBATIM)
add_dependencies(deploy_tearwash tearwash)

add_executable(tearwash_dsp_test src/tests/tearwash_dsp_test_main.cpp)
target_link_libraries(tearwash_dsp_test PRIVATE tearwash_engine)
add_executable(tearwash_gui_test ${TEARWASH_PLUGIN_SOURCES} src/tests/tearwash_gui_test_main.cpp)
tearwash_configure(tearwash_gui_test)
add_executable(tearwash_preset_test ${TEARWASH_PLUGIN_SOURCES} src/tests/tearwash_preset_test_main.cpp)
tearwash_configure(tearwash_preset_test)

if(TEARWASH_TOP_LEVEL)
    enable_testing()
endif()
add_test(NAME tearwash_dsp COMMAND tearwash_dsp_test)
add_test(NAME tearwash_gui COMMAND tearwash_gui_test)
add_test(NAME tearwash_presets COMMAND tearwash_preset_test)
