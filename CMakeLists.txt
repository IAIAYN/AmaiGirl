cmake_minimum_required(VERSION 3.21)
project(AmaiGirl LANGUAGES CXX)

# Ensure minimum macOS version for deployment (downward compatibility)
if(APPLE)
    # Set globally so compilers encode the correct LC_BUILD_VERSION minOS
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version to target" FORCE)
    # Build a universal binary on macOS (Intel + Apple Silicon)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build architectures for macOS" FORCE)
endif()

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt meta build features
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Network Multimedia)

# Live2D Cubism Core SDK
set(CUBISM_SDK_DIR "${CMAKE_SOURCE_DIR}/sdk/cubism")
set(CUBISM_INCLUDE_DIR "${CUBISM_SDK_DIR}/include")

set(CUBISM_LIB_DIR "")
set(CUBISM_CORE_LIB "")
set(CUBISM_CORE_LIB_DEBUG "")
set(CUBISM_CORE_LIB_RELEASE "")

if(APPLE)
    set(_CUBISM_MACOS_ARM64_LIB "${CUBISM_SDK_DIR}/lib/macos/arm64/libLive2DCubismCore.a")
    set(_CUBISM_MACOS_X64_LIB "${CUBISM_SDK_DIR}/lib/macos/x86_64/libLive2DCubismCore.a")

    if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64" AND CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
        find_program(LIPO_EXECUTABLE lipo)
        set(CUBISM_LIB_DIR "${CMAKE_BINARY_DIR}/cubism")
        set(CUBISM_CORE_LIB "${CUBISM_LIB_DIR}/libLive2DCubismCore_universal.a")

        if(NOT LIPO_EXECUTABLE)
            message(FATAL_ERROR "lipo not found, cannot build universal Cubism library")
        endif()
        if(NOT EXISTS "${_CUBISM_MACOS_ARM64_LIB}")
            message(FATAL_ERROR "Cubism arm64 library not found: ${_CUBISM_MACOS_ARM64_LIB}")
        endif()
        if(NOT EXISTS "${_CUBISM_MACOS_X64_LIB}")
            message(FATAL_ERROR "Cubism x86_64 library not found: ${_CUBISM_MACOS_X64_LIB}")
        endif()

        add_custom_command(
            OUTPUT "${CUBISM_CORE_LIB}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CUBISM_LIB_DIR}"
            COMMAND "${LIPO_EXECUTABLE}" -create
                    "${_CUBISM_MACOS_ARM64_LIB}"
                    "${_CUBISM_MACOS_X64_LIB}"
                    -output "${CUBISM_CORE_LIB}"
            DEPENDS "${_CUBISM_MACOS_ARM64_LIB}" "${_CUBISM_MACOS_X64_LIB}"
            COMMENT "Creating universal Cubism Core static library"
            VERBATIM
        )
        add_custom_target(cubism_core_universal DEPENDS "${CUBISM_CORE_LIB}")
    elseif(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
        set(CUBISM_LIB_DIR "${CUBISM_SDK_DIR}/lib/macos/arm64")
        set(CUBISM_CORE_LIB "${_CUBISM_MACOS_ARM64_LIB}")
    elseif(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
        set(CUBISM_LIB_DIR "${CUBISM_SDK_DIR}/lib/macos/x86_64")
        set(CUBISM_CORE_LIB "${_CUBISM_MACOS_X64_LIB}")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(CUBISM_LIB_DIR "${CUBISM_SDK_DIR}/lib/macos/arm64")
        set(CUBISM_CORE_LIB "${_CUBISM_MACOS_ARM64_LIB}")
    else()
        set(CUBISM_LIB_DIR "${CUBISM_SDK_DIR}/lib/macos/x86_64")
        set(CUBISM_CORE_LIB "${_CUBISM_MACOS_X64_LIB}")
    endif()
elseif(WIN32)
    # Cubism Windows libs are grouped by MSVC toolset (e.g. 143)
    set(_CUBISM_WIN_TOOLSET "143")
    if(DEFINED MSVC_TOOLSET_VERSION AND EXISTS "${CUBISM_SDK_DIR}/lib/windows/x86_64/${MSVC_TOOLSET_VERSION}")
        set(_CUBISM_WIN_TOOLSET "${MSVC_TOOLSET_VERSION}")
    endif()
    set(CUBISM_LIB_DIR "${CUBISM_SDK_DIR}/lib/windows/x86_64/${_CUBISM_WIN_TOOLSET}")
    set(CUBISM_CORE_LIB_RELEASE "${CUBISM_LIB_DIR}/Live2DCubismCore_MD.lib")
    set(CUBISM_CORE_LIB_DEBUG "${CUBISM_LIB_DIR}/Live2DCubismCore_MDd.lib")
elseif(UNIX)
    set(CUBISM_LIB_DIR "${CUBISM_SDK_DIR}/lib/linux/x86_64")
    set(CUBISM_CORE_LIB "${CUBISM_LIB_DIR}/libLive2DCubismCore.a")
endif()

# Import static Cubism Core library
add_library(Live2DCubismCore STATIC IMPORTED GLOBAL)
if(WIN32)
    if(EXISTS "${CUBISM_CORE_LIB_RELEASE}")
        set_target_properties(Live2DCubismCore PROPERTIES
            IMPORTED_LOCATION_RELEASE "${CUBISM_CORE_LIB_RELEASE}"
            INTERFACE_INCLUDE_DIRECTORIES "${CUBISM_INCLUDE_DIR}"
        )
        if(EXISTS "${CUBISM_CORE_LIB_DEBUG}")
            set_target_properties(Live2DCubismCore PROPERTIES
                IMPORTED_LOCATION_DEBUG "${CUBISM_CORE_LIB_DEBUG}"
            )
        else()
            set_target_properties(Live2DCubismCore PROPERTIES
                IMPORTED_LOCATION_DEBUG "${CUBISM_CORE_LIB_RELEASE}"
            )
        endif()
    else()
        message(FATAL_ERROR "Cubism Core library not found for Windows: ${CUBISM_CORE_LIB_RELEASE}")
    endif()
elseif(EXISTS "${CUBISM_CORE_LIB}" OR (APPLE AND TARGET cubism_core_universal))
    set_target_properties(Live2DCubismCore PROPERTIES
        IMPORTED_LOCATION "${CUBISM_CORE_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${CUBISM_INCLUDE_DIR}"
    )
    if(APPLE AND TARGET cubism_core_universal)
        add_dependencies(Live2DCubismCore cubism_core_universal)
    endif()
else()
    message(FATAL_ERROR "Cubism Core library not found for this platform. Expected in: ${CUBISM_LIB_DIR}")
endif()

# Sources (reflect new folder layout under src/)
set(APP_SOURCES
    src/app/main.cpp
    src/app/Version.hpp
)
set(ENGINE_SOURCES
    src/engine/Renderer.hpp
    src/engine/Renderer.cpp
    src/engine/Live2DCore.hpp
    src/engine/Live2DCore.cpp
    src/engine/Model.hpp
    src/engine/Model.cpp
    src/engine/Motion.hpp
    src/engine/Motion.cpp
    src/engine/Expression.hpp
    src/engine/Expression.cpp
    src/engine/PhysicsEngine.hpp
    src/engine/PhysicsEngine.cpp
)
set(UI_SOURCES
    src/ui/SettingsWindow.hpp
    src/ui/SettingsWindow.cpp
    src/ui/AboutWindow.hpp
    src/ui/AboutWindow.cpp
    src/ui/ChatWindow.hpp
    src/ui/ChatWindow.cpp
)
set(COMMON_SOURCES
    src/common/Utils.hpp
    src/common/Utils.cpp
    src/common/SettingsManager.hpp
    src/common/SettingsManager.cpp
)

# AI
set(AI_SOURCES
    src/ai/OpenAIChatClient.hpp
    src/ai/OpenAIChatClient.cpp
    src/ai/ChatController.hpp
    src/ai/ChatController.cpp
    src/ai/OpenAITtsClient.hpp
    src/ai/OpenAITtsClient.cpp
)

if(APPLE)
    add_executable(AmaiGirl MACOSX_BUNDLE
        ${APP_SOURCES}
        ${ENGINE_SOURCES}
        ${UI_SOURCES}
        ${COMMON_SOURCES}
        ${AI_SOURCES}
    )
elseif(WIN32)
    add_executable(AmaiGirl WIN32
        ${APP_SOURCES}
        ${ENGINE_SOURCES}
        ${UI_SOURCES}
        ${COMMON_SOURCES}
        ${AI_SOURCES}
    )
else()
    add_executable(AmaiGirl
        ${APP_SOURCES}
        ${ENGINE_SOURCES}
        ${UI_SOURCES}
        ${COMMON_SOURCES}
        ${AI_SOURCES}
    )
endif()

# Also set per-target deployment target as a safeguard
if(APPLE)
    set_target_properties(AmaiGirl PROPERTIES MACOSX_DEPLOYMENT_TARGET "13.0")
    if(TARGET cubism_core_universal)
        add_dependencies(AmaiGirl cubism_core_universal)
    endif()
endif()

# Include dirs: single root only (Scheme A)
set(SRC_ROOT "${PROJECT_SOURCE_DIR}/src")
target_include_directories(AmaiGirl PRIVATE ${SRC_ROOT} ${CUBISM_INCLUDE_DIR})

target_link_libraries(AmaiGirl PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Network
    Qt6::Multimedia
    Live2DCubismCore
)

# macOS specifics
if(APPLE)
    # Link common macOS frameworks if available
    find_library(APPKIT_FRAMEWORK AppKit)
    if(APPKIT_FRAMEWORK)
        target_link_libraries(AmaiGirl PRIVATE ${APPKIT_FRAMEWORK})
    endif()
    find_library(OPENGL_FRAMEWORK OpenGL)
    if(OPENGL_FRAMEWORK)
        target_link_libraries(AmaiGirl PRIVATE ${OPENGL_FRAMEWORK})
    endif()
    # For CGEvent (ApplicationServices)
    find_library(APPSERVICES_FRAMEWORK ApplicationServices)
    if(APPSERVICES_FRAMEWORK)
        target_link_libraries(AmaiGirl PRIVATE ${APPSERVICES_FRAMEWORK})
    endif()

    # For bundled apps, this RPATH helps in Release after deployment
    set_target_properties(AmaiGirl PROPERTIES
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
endif()

# macOS app icon (.icns) generation from PNG and bundle embedding
if(APPLE)
    set(APP_ICON_PNG "${CMAKE_SOURCE_DIR}/res/icons/app-icon.png")
    if(EXISTS "${APP_ICON_PNG}")
        find_program(SIPS_EXECUTABLE sips)
        find_program(ICONUTIL_EXECUTABLE iconutil)
        if(SIPS_EXECUTABLE AND ICONUTIL_EXECUTABLE)
            set(APP_ICONSET_DIR "${CMAKE_CURRENT_BINARY_DIR}/appicon.iconset")
            set(APP_ICNS "${CMAKE_CURRENT_BINARY_DIR}/appicon.icns")
            add_custom_command(OUTPUT "${APP_ICNS}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_ICONSET_DIR}"
                COMMAND "${SIPS_EXECUTABLE}" -s format png -z 16 16     "${APP_ICON_PNG}" --out "${APP_ICONSET_DIR}/icon_16x16.png"
                COMMAND "${SIPS_EXECUTABLE}" -s format png -z 32 32     "${APP_ICON_PNG}" --out "${APP_ICONSET_DIR}/icon_16x16@2x.png"
                COMMAND "${SIPS_EXECUTABLE}" -s format png -z 32 32     "${APP_ICON_PNG}" --out "${APP_ICONSET_DIR}/icon_32x32.png"
                COMMAND "${SIPS_EXECUTABLE}" -s format png -z 64 64     "${APP_ICON_PNG}" --out "${APP_ICONSET_DIR}/icon_32x32@2x.png"
                COMMAND "${SIPS_EXECUTABLE}" -s format png -z 128 128   "${APP_ICON_PNG}" --out "${APP_ICONSET_DIR}/icon_128x128.png"
                COMMAND "${SIPS_EXECUTABLE}" -s format png -z 256 256   "${APP_ICON_PNG}" --out "${APP_ICONSET_DIR}/icon_128x128@2x.png"
                COMMAND "${SIPS_EXECUTABLE}" -s format png -z 256 256   "${APP_ICON_PNG}" --out "${APP_ICONSET_DIR}/icon_256x256.png"
                COMMAND "${SIPS_EXECUTABLE}" -s format png -z 512 512   "${APP_ICON_PNG}" --out "${APP_ICONSET_DIR}/icon_256x256@2x.png"
                COMMAND "${SIPS_EXECUTABLE}" -s format png -z 512 512   "${APP_ICON_PNG}" --out "${APP_ICONSET_DIR}/icon_512x512.png"
                COMMAND "${SIPS_EXECUTABLE}" -s format png -z 1024 1024 "${APP_ICON_PNG}" --out "${APP_ICONSET_DIR}/icon_512x512@2x.png"
                COMMAND "${ICONUTIL_EXECUTABLE}" -c icns "${APP_ICONSET_DIR}" -o "${APP_ICNS}"
                COMMENT "Generating macOS .icns app icon from ${APP_ICON_PNG}"
                VERBATIM
            )
            add_custom_target(app_icon ALL DEPENDS "${APP_ICNS}")
            set_source_files_properties("${APP_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
            target_sources(AmaiGirl PRIVATE "${APP_ICNS}")
            set_target_properties(AmaiGirl PROPERTIES MACOSX_BUNDLE_ICON_FILE "appicon.icns")
        else()
            message(WARNING "未找到 sips 或 iconutil，跳过 .icns 生成，应用将无自定义图标。")
        endif()
    else()
        message(WARNING "未找到应用图标 PNG: ${APP_ICON_PNG}，跳过打包图标。")
    endif()
endif()

# Finalize Qt bundle metadata
qt_finalize_executable(AmaiGirl)

# macOS: supply Info.plist from template
if(APPLE)
    set_target_properties(AmaiGirl PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in"
    )
endif()

# Copy resources into app output
if(APPLE)
    # Bundle layout: AmaiGirl.app/Contents/Resources
    add_custom_command(TARGET AmaiGirl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/res" "$<TARGET_FILE_DIR:AmaiGirl>/../Resources"
        COMMENT "Copying resources to bundle Resources directory"
    )
else()
    add_custom_command(TARGET AmaiGirl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/res" "$<TARGET_FILE_DIR:AmaiGirl>/res"
        COMMENT "Copying resources to output directory"
    )
endif()

# Deploy Qt frameworks/plugins into the .app using macdeployqt ONLY in Release
if(APPLE)
    # Single-config generators (like Ninja used by CLion): rely on CMAKE_BUILD_TYPE
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        # Try to find macdeployqt robustly: PATH first, then derive from Qt6Core_DIR
        set(_MACDEPLOYQT "")
        find_program(_MACDEPLOYQT_PROG macdeployqt)
        if(_MACDEPLOYQT_PROG)
            set(_MACDEPLOYQT "${_MACDEPLOYQT_PROG}")
        endif()
        if(NOT _MACDEPLOYQT AND DEFINED Qt6Core_DIR)
            get_filename_component(_qt6core_cmake_dir "${Qt6Core_DIR}" REALPATH)
            get_filename_component(_qt6core_cmake_parent "${_qt6core_cmake_dir}" DIRECTORY) # …/lib/cmake
            get_filename_component(_qt6_lib_dir "${_qt6core_cmake_parent}" DIRECTORY) # …/lib
            get_filename_component(_qt6_macos_dir "${_qt6_lib_dir}" DIRECTORY) # …/macos
            set(_candidate "${_qt6_macos_dir}/bin/macdeployqt")
            if(EXISTS "${_candidate}")
                set(_MACDEPLOYQT "${_candidate}")
            endif()
        endif()
        if(_MACDEPLOYQT AND EXISTS "${_MACDEPLOYQT}")
            message(STATUS "Using macdeployqt: ${_MACDEPLOYQT}")
            add_custom_command(TARGET AmaiGirl POST_BUILD
                COMMAND "${_MACDEPLOYQT}" "$<TARGET_BUNDLE_DIR:AmaiGirl>" -always-overwrite -verbose=1
                COMMENT "Bundling Qt runtime with macdeployqt (Release)"
            )
        else()
            message(WARNING "未能自动找到 macdeployqt，可执行文件不存在。Release 构建将不打包依赖。若使用 Qt 官方安装器，通常位于 ~/Qt/<ver>/macos/bin/macdeployqt。请将其加入 PATH 或手动执行。")
        endif()
    endif()

    # Multi-config generators (e.g. Xcode/Ninja Multi-Config): conditionally run for Release
    if(CMAKE_CONFIGURATION_TYPES)
        # Resolve macdeployqt same way
        set(_MACDEPLOYQT_MULTI "")
        find_program(_MACDEPLOYQT_PROG_MULTI macdeployqt)
        if(_MACDEPLOYQT_PROG_MULTI)
            set(_MACDEPLOYQT_MULTI "${_MACDEPLOYQT_PROG_MULTI}")
        endif()
        if(NOT _MACDEPLOYQT_MULTI AND DEFINED Qt6Core_DIR)
            get_filename_component(_qt6core_cmake_dir "${Qt6Core_DIR}" REALPATH)
            get_filename_component(_qt6core_cmake_parent "${_qt6core_cmake_dir}" DIRECTORY)
            get_filename_component(_qt6_lib_dir "${_qt6core_cmake_parent}" DIRECTORY)
            get_filename_component(_qt6_macos_dir "${_qt6_lib_dir}" DIRECTORY)
            set(_candidate_multi "${_qt6_macos_dir}/bin/macdeployqt")
            if(EXISTS "${_candidate_multi}")
                set(_MACDEPLOYQT_MULTI "${_candidate_multi}")
            endif()
        endif()
        if(_MACDEPLOYQT_MULTI AND EXISTS "${_MACDEPLOYQT_MULTI}")
            add_custom_command(TARGET AmaiGirl POST_BUILD
                COMMAND /bin/sh -c "if [ \"$<CONFIG>\" = \"Release\" ]; then \"${_MACDEPLOYQT_MULTI}\" \"$<TARGET_BUNDLE_DIR:AmaiGirl>\" -always-overwrite -verbose=1; fi"
                COMMENT "Bundling Qt runtime with macdeployqt (Release)"
            )
        endif()
    endif()
endif()
