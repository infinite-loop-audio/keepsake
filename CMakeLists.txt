cmake_minimum_required(VERSION 3.24)

project(keepsake
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "CLAP plugin that bridges legacy plugins into modern hosts"
)

if(APPLE)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestPlugin-Info.plist.in
        ${CMAKE_CURRENT_BINARY_DIR}/TestPlugin-Info.plist
        @ONLY
    )
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/DemoVst2GuiPlugin-Info.plist.in
        ${CMAKE_CURRENT_BINARY_DIR}/DemoVst2GuiPlugin-Info.plist
        @ONLY
    )
endif()

execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE KEEPSAKE_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT KEEPSAKE_GIT_HASH)
    set(KEEPSAKE_GIT_HASH "unknown")
endif()

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
if(WIN32)
    file(COPY
        "${vst3_pluginterfaces_SOURCE_DIR}/"
        DESTINATION "${CMAKE_BINARY_DIR}/_deps/vst3-include/pluginterfaces"
    )
else()
    file(CREATE_LINK
        "${vst3_pluginterfaces_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}/_deps/vst3-include/pluginterfaces"
        SYMBOLIC
    )
endif()

# --- Main plugin target ---

add_library(keepsake MODULE
    src/main.cpp
    src/factory.cpp
    src/factory_helpers.cpp
    src/plugin_labels.cpp
    src/factory_scan.cpp
    src/vst2_loader.cpp
    src/vst2_loader_platform.cpp
    src/vst2_loader_bridge.cpp
    src/plugin.cpp
    src/plugin_editor_session.cpp
    src/plugin_callbacks.cpp
    src/plugin_processing.cpp
    src/plugin_async.cpp
    src/plugin_params.cpp
    src/plugin_state.cpp
    src/config.cpp
    src/config_cache.cpp
    src/bridge_pool.cpp
)

target_include_directories(keepsake PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(keepsake PRIVATE
    KEEPSAKE_BUILD_ID="${KEEPSAKE_GIT_HASH}"
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
    target_sources(keepsake PRIVATE
        src/plugin_gui.cpp
        src/plugin_gui_mac_embed.mm)
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
        "-framework IOSurface"
        "-framework QuartzCore"
        "-framework Cocoa"
    )

    # Universal binary support (x86_64 + arm64)
    # Set CMAKE_OSX_ARCHITECTURES externally or via preset to build universal
    if(NOT CMAKE_OSX_ARCHITECTURES)
        # Default to native architecture for development
        set(CMAKE_OSX_ARCHITECTURES "${CMAKE_SYSTEM_PROCESSOR}" CACHE STRING "")
    endif()

elseif(WIN32)
    target_sources(keepsake PRIVATE src/plugin_gui.cpp)
    set_target_properties(keepsake PROPERTIES
        SUFFIX ".clap"
        OUTPUT_NAME "keepsake"
    )

else()
    # Linux
    target_sources(keepsake PRIVATE src/plugin_gui.cpp)
    set_target_properties(keepsake PROPERTIES
        SUFFIX ".clap"
        OUTPUT_NAME "keepsake"
    )

    target_link_libraries(keepsake PRIVATE dl)
endif()

# --- Bridge helper binary ---

if(APPLE)
    set(BRIDGE_GUI_SRC
        src/bridge_gui_mac.mm
        src/bridge_gui_mac_window.mm
        src/bridge_gui_mac_window_views.mm
        src/bridge_gui_mac_iosurface.mm)
    set(BRIDGE_AU_SRC  src/bridge_loader_au.mm)
else()
    # Windows and Linux GUI with embedded editor support
    set(BRIDGE_GUI_SRC
        src/bridge_gui_stub.cpp
        src/bridge_gui_stub_windows.cpp
        src/bridge_gui_stub_linux.cpp)
    set(BRIDGE_AU_SRC)
endif()

add_executable(keepsake-bridge
    src/bridge_main.cpp
    src/plugin_labels.cpp
    src/bridge_runtime.cpp
    src/bridge_runtime_init.cpp
    src/bridge_runtime_audio.cpp
    src/bridge_loader_vst2.cpp
    src/bridge_loader_vst2_platform.cpp
    src/bridge_loader_vst3.cpp
    src/bridge_loader_vst3_module.cpp
    src/bridge_loader_vst3_lifecycle.cpp
    src/bridge_loader_factory.cpp
    ${BRIDGE_AU_SRC}
    ${BRIDGE_GUI_SRC}
)

target_include_directories(keepsake-bridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/vst3-include
)
target_compile_definitions(keepsake-bridge PRIVATE
    KEEPSAKE_BUILD_ID="${KEEPSAKE_GIT_HASH}"
)

target_link_libraries(keepsake-bridge PRIVATE clap)

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set_target_properties(keepsake-bridge PROPERTIES
        OUTPUT_NAME "keepsake-bridge-32"
    )
endif()

if(APPLE)
    target_link_libraries(keepsake-bridge PRIVATE
        "-framework CoreFoundation"
        "-framework Cocoa"
        "-framework AudioToolbox"
        "-framework IOSurface"
        "-framework QuartzCore"
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
        src/plugin_labels.cpp
        src/bridge_runtime.cpp
        src/bridge_runtime_init.cpp
        src/bridge_runtime_audio.cpp
        src/bridge_loader_vst2.cpp
        src/bridge_loader_vst2_platform.cpp
        src/bridge_loader_vst3.cpp
        src/bridge_loader_vst3_module.cpp
        src/bridge_loader_vst3_lifecycle.cpp
        src/bridge_loader_factory.cpp
        src/bridge_loader_au.mm
        src/bridge_gui_mac.mm
        src/bridge_gui_mac_window.mm
        src/bridge_gui_mac_window_views.mm
        src/bridge_gui_mac_iosurface.mm
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
        "-framework IOSurface"
        "-framework QuartzCore"
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

add_executable(clap-scan tools/clap-scan.cpp)
target_link_libraries(clap-scan PRIVATE clap)
if(NOT WIN32)
    target_link_libraries(clap-scan PRIVATE dl)
endif()

add_library(test-plugin MODULE tools/test-plugin.cpp)
target_include_directories(test-plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
set_target_properties(test-plugin PROPERTIES PREFIX "")
if(APPLE)
    set_target_properties(test-plugin PROPERTIES
        SUFFIX ".dylib"
        BUNDLE FALSE
    )
    add_custom_command(TARGET test-plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/test-plugin.vst/Contents/MacOS"
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:test-plugin>"
            "${CMAKE_CURRENT_BINARY_DIR}/test-plugin.vst/Contents/MacOS/test-plugin"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_BINARY_DIR}/TestPlugin-Info.plist"
            "${CMAKE_CURRENT_BINARY_DIR}/test-plugin.vst/Contents/Info.plist"
        COMMENT "Packaging test-plugin as a macOS VST bundle"
    )
elseif(WIN32)
    set_target_properties(test-plugin PROPERTIES SUFFIX ".dll")
else()
    set_target_properties(test-plugin PROPERTIES SUFFIX ".so")
endif()

if(APPLE)
    add_library(demo-vst2-gui-plugin MODULE tools/demo-vst2-gui-plugin.mm)
    target_include_directories(demo-vst2-gui-plugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    )
    target_link_libraries(demo-vst2-gui-plugin PRIVATE
        "-framework Cocoa"
        "-framework QuartzCore"
    )
    set_target_properties(demo-vst2-gui-plugin PROPERTIES
        PREFIX ""
        SUFFIX ".dylib"
        BUNDLE FALSE
    )
    add_custom_command(TARGET demo-vst2-gui-plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/demo-vst2-gui-plugin.vst/Contents/MacOS"
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:demo-vst2-gui-plugin>"
            "${CMAKE_CURRENT_BINARY_DIR}/demo-vst2-gui-plugin.vst/Contents/MacOS/demo-vst2-gui-plugin"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_BINARY_DIR}/DemoVst2GuiPlugin-Info.plist"
            "${CMAKE_CURRENT_BINARY_DIR}/demo-vst2-gui-plugin.vst/Contents/Info.plist"
        COMMENT "Packaging demo-vst2-gui-plugin as a macOS VST bundle"
    )
endif()

if(WIN32)
    add_executable(windows-vst2-probe tools/windows-vst2-probe.cpp)
    target_include_directories(windows-vst2-probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    )

    add_executable(windows-clap-host tools/windows-clap-host.cpp)
    target_include_directories(windows-clap-host PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    )
    target_link_libraries(windows-clap-host PRIVATE clap)

    add_library(host-probe-clap MODULE tools/host-probe-clap.cpp)
    target_include_directories(host-probe-clap PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    )
    target_link_libraries(host-probe-clap PRIVATE clap)
    set_target_properties(host-probe-clap PROPERTIES
        PREFIX ""
        SUFFIX ".clap"
        OUTPUT_NAME "host-probe"
    )
endif()

if(APPLE OR UNIX)
    add_executable(bridge-test tools/bridge-test.cpp)
    target_include_directories(bridge-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(bridge-test PRIVATE clap)

    add_executable(keepsake-scan tools/keepsake-scan.cpp
        src/vst2_loader.cpp
        src/vst2_loader_platform.cpp
        src/vst2_loader_bridge.cpp
        src/config.cpp
        src/config_cache.cpp)
    target_include_directories(keepsake-scan PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/_deps/vst3-include
    )
    target_link_libraries(keepsake-scan PRIVATE clap dl)
    if(APPLE)
        target_link_libraries(keepsake-scan PRIVATE "-framework CoreFoundation")
    endif()

    add_executable(host-test tools/host-test.cpp)
    target_link_libraries(host-test PRIVATE clap dl)

    add_executable(host-test-threaded tools/host-test-threaded.cpp)
    target_link_libraries(host-test-threaded PRIVATE clap dl)

    if(APPLE)
        add_executable(mac-clap-host tools/mac-clap-host.mm)
        target_link_libraries(mac-clap-host PRIVATE
            clap
            dl
            "-framework Cocoa"
            "-framework QuartzCore"
        )
    endif()
endif()
