cmake_minimum_required(VERSION 3.21)
project(AmaiGirl LANGUAGES CXX)

if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR
        "Windows builds currently require MSVC (Visual Studio 2022 / v143 or newer). "
        "The Live2D Cubism Core libraries used by this project are provided as MSVC import libraries, "
        "so MinGW/GCC toolchains are not supported."
    )
endif()

option(AMAIGIRL_ENABLE_APPIMAGE "Enable Linux AppImage packaging target" ON)
option(AMAIGIRL_APPIMAGE_CLEAN "Clean AppDir before packaging AppImage" ON)
option(AMAIGIRL_ENABLE_WINDEPLOYQT "Enable Windows deployment/package targets" ON)
set(AMAIGIRL_LINUXDEPLOY_EXECUTABLE "" CACHE FILEPATH "Optional path override for linuxdeploy executable")
set(AMAIGIRL_APPIMAGETOOL_EXECUTABLE "" CACHE FILEPATH "Optional path override for appimagetool executable")
set(AMAIGIRL_QMAKE_EXECUTABLE "" CACHE FILEPATH "Optional path override for qmake/qmake6 executable")
set(AMAIGIRL_WINDEPLOYQT_EXECUTABLE "" CACHE FILEPATH "Optional path override for windeployqt executable")

# Ensure minimum macOS version for deployment (downward compatibility)
if(APPLE)
    # Set globally so compilers encode the correct LC_BUILD_VERSION minOS
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version to target" FORCE)
    # Build a universal binary on macOS (Intel + Apple Silicon)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build architectures for macOS" FORCE)
endif()

# Linux AppImage packaging (optional)
if(UNIX AND NOT APPLE AND AMAIGIRL_ENABLE_APPIMAGE)
    set(_APPIMAGE_ARCH "x86_64")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(_APPIMAGE_ARCH "aarch64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
        set(_APPIMAGE_ARCH "x86_64")
    else()
        set(_APPIMAGE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    if(AMAIGIRL_LINUXDEPLOY_EXECUTABLE)
        set(LINUXDEPLOY_EXECUTABLE "${AMAIGIRL_LINUXDEPLOY_EXECUTABLE}")
    else()
        find_program(LINUXDEPLOY_EXECUTABLE
            NAMES
                linuxdeploy
                linuxdeploy.AppImage
                linuxdeploy-x86_64.AppImage
                linuxdeploy-aarch64.AppImage
        )
    endif()

    if(AMAIGIRL_APPIMAGETOOL_EXECUTABLE)
        set(APPIMAGETOOL_EXECUTABLE "${AMAIGIRL_APPIMAGETOOL_EXECUTABLE}")
    else()
        find_program(APPIMAGETOOL_EXECUTABLE
            NAMES
                appimagetool
                appimagetool.AppImage
                appimagetool-x86_64.AppImage
                appimagetool-aarch64.AppImage
        )
    endif()

    if(LINUXDEPLOY_EXECUTABLE AND APPIMAGETOOL_EXECUTABLE)
        set(AMAIGIRL_APPDIR "${CMAKE_BINARY_DIR}/AppDir")
        set(AMAIGIRL_DESKTOP_FILE "${CMAKE_SOURCE_DIR}/cmake/amaigirl.desktop")
        set(AMAIGIRL_ICON_FILE "${CMAKE_SOURCE_DIR}/res/icons/app-icon.png")

        set(_AMAIGIRL_APPIMAGE_CLEAN_CMD "")
        if(AMAIGIRL_APPIMAGE_CLEAN)
            set(_AMAIGIRL_APPIMAGE_CLEAN_CMD
                COMMAND ${CMAKE_COMMAND} -E rm -rf "${AMAIGIRL_APPDIR}"
            )
        endif()

        if(NOT AMAIGIRL_QMAKE_EXECUTABLE)
            set(_AMAIGIRL_QT_BIN_HINTS "")
            foreach(_qt_bin_candidate
                "$ENV{QTDIR}/bin"
                "$ENV{QT_DIR}/bin"
                "$ENV{QT_ROOT_DIR}/bin"
                "$ENV{QT_ROOT}/bin")
                if(_qt_bin_candidate AND EXISTS "${_qt_bin_candidate}")
                    list(APPEND _AMAIGIRL_QT_BIN_HINTS "${_qt_bin_candidate}")
                endif()
            endforeach()

            if(Qt6Core_DIR)
                get_filename_component(_qt6core_dir_real "${Qt6Core_DIR}" REALPATH)
                get_filename_component(_qt6core_cmake_dir "${_qt6core_dir_real}" DIRECTORY)
                get_filename_component(_qt6_lib_dir "${_qt6core_cmake_dir}" DIRECTORY)
                get_filename_component(_qt6_root_dir "${_qt6_lib_dir}" DIRECTORY)
                if(EXISTS "${_qt6_root_dir}/bin")
                    list(APPEND _AMAIGIRL_QT_BIN_HINTS "${_qt6_root_dir}/bin")
                endif()
            endif()

            find_program(AMAIGIRL_QMAKE_EXECUTABLE
                NAMES qmake6 qmake
                HINTS ${_AMAIGIRL_QT_BIN_HINTS}
            )
        endif()

        if(AMAIGIRL_QMAKE_EXECUTABLE)
            set(_AMAIGIRL_LINUXDEPLOY_ENV_QT
                COMMAND ${CMAKE_COMMAND} -E env ARCH=${_APPIMAGE_ARCH} QMAKE=${AMAIGIRL_QMAKE_EXECUTABLE}
            )

            execute_process(
                COMMAND "${AMAIGIRL_QMAKE_EXECUTABLE}" -query QT_INSTALL_PLUGINS
                OUTPUT_VARIABLE AMAIGIRL_QT_PLUGINS_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            execute_process(
                COMMAND "${AMAIGIRL_QMAKE_EXECUTABLE}" -query QT_INSTALL_LIBS
                OUTPUT_VARIABLE AMAIGIRL_QT_LIBS_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        else()
            set(_AMAIGIRL_LINUXDEPLOY_ENV_QT
                COMMAND ${CMAKE_COMMAND} -E env ARCH=${_APPIMAGE_ARCH}
            )
            message(WARNING "qmake not found; linuxdeploy --plugin qt may fail")
        endif()

        set(_AMAIGIRL_COPY_WAYLAND_CMDS "")
        if(AMAIGIRL_QT_PLUGINS_DIR AND EXISTS "${AMAIGIRL_QT_PLUGINS_DIR}")
            set(_AMAIGIRL_QT_PLATFORMS_DIR "${AMAIGIRL_QT_PLUGINS_DIR}/platforms")
            if(EXISTS "${_AMAIGIRL_QT_PLATFORMS_DIR}/libqwayland-generic.so")
                list(APPEND _AMAIGIRL_COPY_WAYLAND_CMDS
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${AMAIGIRL_APPDIR}/usr/plugins/platforms"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_AMAIGIRL_QT_PLATFORMS_DIR}/libqwayland-generic.so" "${AMAIGIRL_APPDIR}/usr/plugins/platforms/libqwayland-generic.so"
                )
            endif()
            if(EXISTS "${_AMAIGIRL_QT_PLATFORMS_DIR}/libqwayland-egl.so")
                list(APPEND _AMAIGIRL_COPY_WAYLAND_CMDS
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${AMAIGIRL_APPDIR}/usr/plugins/platforms"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_AMAIGIRL_QT_PLATFORMS_DIR}/libqwayland-egl.so" "${AMAIGIRL_APPDIR}/usr/plugins/platforms/libqwayland-egl.so"
                )
            endif()

            foreach(_wayland_subdir
                wayland-shell-integration
                wayland-decoration-client
                wayland-graphics-integration-client
                wayland-graphics-integration-server)
                if(EXISTS "${AMAIGIRL_QT_PLUGINS_DIR}/${_wayland_subdir}")
                    list(APPEND _AMAIGIRL_COPY_WAYLAND_CMDS
                        COMMAND ${CMAKE_COMMAND} -E copy_directory "${AMAIGIRL_QT_PLUGINS_DIR}/${_wayland_subdir}" "${AMAIGIRL_APPDIR}/usr/plugins/${_wayland_subdir}"
                    )
                endif()
            endforeach()
        else()
            message(WARNING "Qt plugins directory not found; cannot prebundle Wayland plugins into AppImage")
        endif()

        if(AMAIGIRL_QT_LIBS_DIR AND EXISTS "${AMAIGIRL_QT_LIBS_DIR}")
            list(APPEND _AMAIGIRL_COPY_WAYLAND_CMDS
                COMMAND ${CMAKE_COMMAND} -E make_directory "${AMAIGIRL_APPDIR}/usr/lib"
            )
            foreach(_wayland_lib
                libQt6WaylandClient.so
                libQt6WaylandClient.so.6
                libQt6WaylandEglClientHwIntegration.so
                libQt6WaylandEglClientHwIntegration.so.6)
                if(EXISTS "${AMAIGIRL_QT_LIBS_DIR}/${_wayland_lib}")
                    list(APPEND _AMAIGIRL_COPY_WAYLAND_CMDS
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${AMAIGIRL_QT_LIBS_DIR}/${_wayland_lib}" "${AMAIGIRL_APPDIR}/usr/lib/${_wayland_lib}"
                    )
                endif()
            endforeach()
        else()
            message(WARNING "Qt libs directory not found; cannot prebundle Qt Wayland runtime libs into AppImage")
        endif()

        add_custom_target(package_appimage
            ${_AMAIGIRL_APPIMAGE_CLEAN_CMD}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${AMAIGIRL_APPDIR}/usr/bin"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${AMAIGIRL_APPDIR}/usr/share/icons/hicolor/256x256/apps"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:AmaiGirl>" "${AMAIGIRL_APPDIR}/usr/bin/AmaiGirl"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/res" "${AMAIGIRL_APPDIR}/usr/bin/res"
            COMMAND ${CMAKE_COMMAND} -E copy "${AMAIGIRL_DESKTOP_FILE}" "${AMAIGIRL_APPDIR}/amaigirl.desktop"
            COMMAND ${CMAKE_COMMAND} -E copy "${AMAIGIRL_ICON_FILE}" "${AMAIGIRL_APPDIR}/amaigirl.png"
            COMMAND ${CMAKE_COMMAND} -E copy "${AMAIGIRL_ICON_FILE}" "${AMAIGIRL_APPDIR}/.DirIcon"
            COMMAND ${CMAKE_COMMAND} -E copy "${AMAIGIRL_ICON_FILE}" "${AMAIGIRL_APPDIR}/usr/share/icons/hicolor/256x256/apps/amaigirl.png"
            ${_AMAIGIRL_LINUXDEPLOY_ENV_QT}
                    "${LINUXDEPLOY_EXECUTABLE}"
                    --appdir "${AMAIGIRL_APPDIR}"
                    --desktop-file "${AMAIGIRL_APPDIR}/amaigirl.desktop"
                    --icon-file "${AMAIGIRL_APPDIR}/amaigirl.png"
                    --executable "${AMAIGIRL_APPDIR}/usr/bin/AmaiGirl"
                    --plugin qt
                ${_AMAIGIRL_COPY_WAYLAND_CMDS}
            COMMAND ${CMAKE_COMMAND} -E copy "${AMAIGIRL_DESKTOP_FILE}" "${AMAIGIRL_APPDIR}/amaigirl.desktop"
            COMMAND ${CMAKE_COMMAND} -E copy "${AMAIGIRL_ICON_FILE}" "${AMAIGIRL_APPDIR}/amaigirl.png"
            COMMAND ${CMAKE_COMMAND} -E copy "${AMAIGIRL_ICON_FILE}" "${AMAIGIRL_APPDIR}/.DirIcon"
            COMMAND ${CMAKE_COMMAND} -E copy "${AMAIGIRL_ICON_FILE}" "${AMAIGIRL_APPDIR}/usr/share/icons/hicolor/256x256/apps/amaigirl.png"
            COMMAND ${CMAKE_COMMAND} -E env ARCH=${_APPIMAGE_ARCH}
                    "${LINUXDEPLOY_EXECUTABLE}"
                    --appdir "${AMAIGIRL_APPDIR}"
                    --output appimage
            DEPENDS AmaiGirl
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Packaging AppImage via linuxdeploy"
            VERBATIM
        )

        message(STATUS "Linux AppImage target enabled: package_appimage")
    else()
        message(STATUS "AppImage tools not found (need linuxdeploy + appimagetool), skip package_appimage target")
    endif()
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

if(WIN32)
    target_sources(AmaiGirl PRIVATE
        cmake/AmaiGirl.rc
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

if(MSVC)
    target_compile_options(AmaiGirl PRIVATE /Zc:__cplusplus)
endif()

if(WIN32)
    target_compile_definitions(AmaiGirl PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

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

# Windows: deploy Qt runtime and package a portable zip via windeployqt
if(WIN32 AND AMAIGIRL_ENABLE_WINDEPLOYQT)
    set(_AMAIGIRL_MSVC_REDIST_CRT_DIR "")
    if(MSVC)
        get_filename_component(_amaigirl_msvc_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(_amaigirl_msvc_host_dir "${_amaigirl_msvc_bin_dir}" DIRECTORY)
        get_filename_component(_amaigirl_msvc_arch_dir "${_amaigirl_msvc_host_dir}" DIRECTORY)
        get_filename_component(_amaigirl_msvc_bin_root "${_amaigirl_msvc_arch_dir}" DIRECTORY)
        get_filename_component(_amaigirl_msvc_tools_dir "${_amaigirl_msvc_bin_root}" DIRECTORY)
        get_filename_component(_amaigirl_msvc_version_dir "${_amaigirl_msvc_tools_dir}" DIRECTORY)
        get_filename_component(_amaigirl_vc_root_dir "${_amaigirl_msvc_version_dir}" DIRECTORY)
        set(_amaigirl_redist_root "${_amaigirl_vc_root_dir}/Redist/MSVC")

        if(EXISTS "${_amaigirl_redist_root}")
            file(GLOB _amaigirl_redist_versions LIST_DIRECTORIES true "${_amaigirl_redist_root}/*")
            list(SORT _amaigirl_redist_versions ORDER DESCENDING)
            foreach(_amaigirl_redist_candidate ${_amaigirl_redist_versions})
                set(_amaigirl_redist_crt_dir "${_amaigirl_redist_candidate}/x64/Microsoft.VC143.CRT")
                if(EXISTS "${_amaigirl_redist_crt_dir}")
                    set(_AMAIGIRL_MSVC_REDIST_CRT_DIR "${_amaigirl_redist_crt_dir}")
                    break()
                endif()
            endforeach()
        endif()
    endif()

    if(AMAIGIRL_WINDEPLOYQT_EXECUTABLE)
        set(WINDEPLOYQT_EXECUTABLE "${AMAIGIRL_WINDEPLOYQT_EXECUTABLE}")
    else()
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt)
        if(NOT WINDEPLOYQT_EXECUTABLE AND DEFINED Qt6Core_DIR)
            get_filename_component(_qt6core_dir_real "${Qt6Core_DIR}" REALPATH)
            get_filename_component(_qt6core_cmake_dir "${_qt6core_dir_real}" DIRECTORY)
            get_filename_component(_qt6_lib_dir "${_qt6core_cmake_dir}" DIRECTORY)
            get_filename_component(_qt6_root_dir "${_qt6_lib_dir}" DIRECTORY)
            set(_windeployqt_candidate "${_qt6_root_dir}/bin/windeployqt.exe")
            if(EXISTS "${_windeployqt_candidate}")
                set(WINDEPLOYQT_EXECUTABLE "${_windeployqt_candidate}")
            endif()
        endif()
    endif()

    if(WINDEPLOYQT_EXECUTABLE AND EXISTS "${WINDEPLOYQT_EXECUTABLE}")
        set(AMAIGIRL_WINDOWS_STAGE_DIR "${CMAKE_BINARY_DIR}/AmaiGirl")
        set(AMAIGIRL_WINDOWS_PACKAGE_ZIP "${CMAKE_BINARY_DIR}/AmaiGirl-windows.zip")
        set(_AMAIGIRL_DEPLOY_WINDOWS_REDIST_COMMANDS "")
        set(_AMAIGIRL_PACKAGE_WINDOWS_REDIST_COMMANDS "")
        if(_AMAIGIRL_MSVC_REDIST_CRT_DIR)
            set(_AMAIGIRL_DEPLOY_WINDOWS_REDIST_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${_AMAIGIRL_MSVC_REDIST_CRT_DIR}" "$<TARGET_FILE_DIR:AmaiGirl>"
            )
            set(_AMAIGIRL_PACKAGE_WINDOWS_REDIST_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${_AMAIGIRL_MSVC_REDIST_CRT_DIR}" "${AMAIGIRL_WINDOWS_STAGE_DIR}"
            )
        endif()

        add_custom_target(deploy_windows
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --dir "$<TARGET_FILE_DIR:AmaiGirl>"
                    --compiler-runtime
                    --no-translations
                    --no-opengl-sw
                    "$<TARGET_FILE:AmaiGirl>"
            ${_AMAIGIRL_DEPLOY_WINDOWS_REDIST_COMMANDS}
            DEPENDS AmaiGirl
            COMMENT "Deploying Qt runtime with windeployqt"
            VERBATIM
        )

        add_custom_target(package_windows
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${AMAIGIRL_WINDOWS_STAGE_DIR}"
            COMMAND ${CMAKE_COMMAND} -E rm -f "${AMAIGIRL_WINDOWS_PACKAGE_ZIP}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${AMAIGIRL_WINDOWS_STAGE_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:AmaiGirl>" "${AMAIGIRL_WINDOWS_STAGE_DIR}/AmaiGirl.exe"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/res" "${AMAIGIRL_WINDOWS_STAGE_DIR}/res"
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --dir "${AMAIGIRL_WINDOWS_STAGE_DIR}"
                    --compiler-runtime
                    --no-translations
                    --no-opengl-sw
                    "${AMAIGIRL_WINDOWS_STAGE_DIR}/AmaiGirl.exe"
                ${_AMAIGIRL_PACKAGE_WINDOWS_REDIST_COMMANDS}
            COMMAND ${CMAKE_COMMAND} -E tar cf "${AMAIGIRL_WINDOWS_PACKAGE_ZIP}" --format=zip "AmaiGirl"
            DEPENDS AmaiGirl
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Packaging Windows portable bundle"
            VERBATIM
        )

        message(STATUS "Windows deployment targets enabled: deploy_windows, package_windows")
    else()
        message(STATUS "windeployqt not found, skip deploy_windows/package_windows targets")
    endif()
endif()

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
