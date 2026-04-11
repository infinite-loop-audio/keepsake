cmake_minimum_required(VERSION 3.24)

project(keepsake
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "CLAP plugin that bridges legacy plugins into modern hosts"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# --- Dependencies ---

include(FetchContent)

FetchContent_Declare(
    clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG 1.2.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(clap)

# VST3 pluginterfaces (header-only, for bridge VST3 loading)
FetchContent_Declare(
    vst3_pluginterfaces
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3_pluginterfaces.git
    GIT_TAG v3.8.0_build_66
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(vst3_pluginterfaces)

# Create a symlink so #include "pluginterfaces/..." works
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps/vst3-include")
file(CREATE_LINK
    "${vst3_pluginterfaces_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/_deps/vst3-include/pluginterfaces"
    SYMBOLIC
)

# --- Main plugin target ---

add_library(keepsake MODULE
    src/main.cpp
    src/factory.cpp
    src/vst2_loader.cpp
    src/plugin.cpp
    src/config.cpp
    src/bridge_pool.cpp
)

target_include_directories(keepsake PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(keepsake PRIVATE clap)

# Hide all symbols by default; only clap_entry is exported (via CLAP_EXPORT)
set_target_properties(keepsake PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    PREFIX ""
)

# --- Platform-specific configuration ---

if(APPLE)
    set_target_properties(keepsake PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION clap
        MACOSX_BUNDLE_GUI_IDENTIFIER "audio.infiniteloop.keepsake"
        MACOSX_BUNDLE_BUNDLE_NAME "Keepsake"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
    )

    target_link_libraries(keepsake PRIVATE
        "-framework CoreFoundation"
        "-framework AudioToolbox"
    )

    # Universal binary support (x86_64 + arm64)
    # Set CMAKE_OSX_ARCHITECTURES externally or via preset to build universal
    if(NOT CMAKE_OSX_ARCHITECTURES)
        # Default to native architecture for development
        set(CMAKE_OSX_ARCHITECTURES "${CMAKE_SYSTEM_PROCESSOR}" CACHE STRING "")
    endif()

elseif(WIN32)
    set_target_properties(keepsake PROPERTIES
        SUFFIX ".clap"
        OUTPUT_NAME "keepsake"
    )

else()
    # Linux
    set_target_properties(keepsake PROPERTIES
        SUFFIX ".clap"
        OUTPUT_NAME "keepsake"
    )

    target_link_libraries(keepsake PRIVATE dl)
endif()

# --- Bridge helper binary ---

if(APPLE)
    set(BRIDGE_GUI_SRC src/bridge_gui_mac.mm)
    set(BRIDGE_AU_SRC  src/bridge_loader_au.mm)
else()
    # Windows and Linux GUI with embedded editor support
    set(BRIDGE_GUI_SRC src/bridge_gui_stub.cpp)
    set(BRIDGE_AU_SRC)
endif()

add_executable(keepsake-bridge
    src/bridge_main.cpp
    src/bridge_loader_vst2.cpp
    src/bridge_loader_vst3.cpp
    src/bridge_loader_factory.cpp
    ${BRIDGE_AU_SRC}
    ${BRIDGE_GUI_SRC}
)

target_include_directories(keepsake-bridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/vst3-include
)

target_link_libraries(keepsake-bridge PRIVATE clap)

if(APPLE)
    target_link_libraries(keepsake-bridge PRIVATE
        "-framework CoreFoundation"
        "-framework Cocoa"
        "-framework AudioToolbox"
    )

    # Install bridge binary inside the .clap bundle
    add_custom_command(TARGET keepsake-bridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:keepsake>/Contents/Helpers"
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:keepsake-bridge>"
            "$<TARGET_BUNDLE_DIR:keepsake>/Contents/Helpers/keepsake-bridge"
        COMMENT "Installing keepsake-bridge into .clap bundle"
    )
elseif(UNIX)
    find_package(X11 QUIET)
    target_link_libraries(keepsake-bridge PRIVATE dl rt)
    if(X11_FOUND)
        target_link_libraries(keepsake-bridge PRIVATE ${X11_LIBRARIES})
        target_include_directories(keepsake-bridge PRIVATE ${X11_INCLUDE_DIR})
    endif()
endif()

# --- macOS cross-architecture bridge (x86_64 under Rosetta) ---

if(APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    # Build an x86_64 bridge binary for loading x86_64 VST2 plugins
    add_executable(keepsake-bridge-x86_64
        src/bridge_main.cpp
        src/bridge_loader_vst2.cpp
        src/bridge_loader_vst3.cpp
        src/bridge_loader_factory.cpp
        src/bridge_loader_au.mm
        src/bridge_gui_mac.mm
    )

    target_include_directories(keepsake-bridge-x86_64 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/_deps/vst3-include
    )

    target_link_libraries(keepsake-bridge-x86_64 PRIVATE clap)
    target_link_libraries(keepsake-bridge-x86_64 PRIVATE
        "-framework CoreFoundation"
        "-framework Cocoa"
        "-framework AudioToolbox"
    )

    set_target_properties(keepsake-bridge-x86_64 PROPERTIES
        OSX_ARCHITECTURES "x86_64"
    )

    add_custom_command(TARGET keepsake-bridge-x86_64 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:keepsake-bridge-x86_64>"
            "$<TARGET_BUNDLE_DIR:keepsake>/Contents/Helpers/keepsake-bridge-x86_64"
        COMMENT "Installing x86_64 bridge into .clap bundle (Rosetta)"
    )
endif()

# --- Development tools (not part of the plugin) ---

if(APPLE OR UNIX)
    add_executable(clap-scan tools/clap-scan.cpp)
    target_link_libraries(clap-scan PRIVATE clap)
    target_link_libraries(clap-scan PRIVATE dl)

    add_executable(bridge-test tools/bridge-test.cpp)
    target_include_directories(bridge-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(bridge-test PRIVATE clap)

    # Test plugin — minimal VST2 for CI
    add_library(test-plugin MODULE tools/test-plugin.cpp)
    target_include_directories(test-plugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    )
    set_target_properties(test-plugin PROPERTIES PREFIX "" SUFFIX ".so")
    if(APPLE)
        set_target_properties(test-plugin PROPERTIES
            SUFFIX ".dylib"
            BUNDLE FALSE
        )
    endif()
endif()
