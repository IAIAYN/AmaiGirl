# Contributing Guide

<p><a href="CONTRIBUTING.md#zh-cn">简体中文</a> | <a href="CONTRIBUTING.en.md#en-us">English</a></p>

<a id="en-us"></a>

## English

Thank you for your interest in contributing to AmaiGirl.

AmaiGirl aims to become a cross-platform native AI desktop assistant. The current implementation is available on macOS, Windows, and Linux.

### 1. Prerequisites

#### macOS

- Build environment: Xcode 15 (macOS 14 SDK) or newer
- Build tools: CMake + Ninja + Clang
- Qt: Qt 6 (Core / Gui / Widgets / OpenGL / OpenGLWidgets / Network / Multimedia / Svg)

#### Windows

- Recommended environment: Windows 10/11 (x86_64)
- Build tools: Visual Studio 2022 (MSVC v143) or newer, or CMake + Ninja inside the matching MSVC developer environment
- MinGW/GCC is not supported at the moment because the Live2D Cubism Windows libraries used by this repository are built for MSVC
- Qt: Qt 6 (Core / Gui / Widgets / OpenGL / OpenGLWidgets / Network / Multimedia / Svg), with `windeployqt` available
- Live2D: make sure `sdk/cubism/lib/windows/x86_64/<toolset>/` contains the matching MSVC toolset libraries `Live2DCubismCore_MD.lib` / `Live2DCubismCore_MDd.lib`

#### Linux

- Recommended environment: Wayland session (X11 can be used as fallback backend)
- Build tools: CMake + Ninja + GCC/Clang
- Qt: Qt 6 (Core / Gui / Widgets / OpenGL / OpenGLWidgets / Network / Multimedia / Svg)
- Optional packaging tools (AppImage): `linuxdeploy`, `appimagetool`

### 2. **Live2D SDK (Important, must be downloaded manually)**

Due to licensing constraints, this repository does not directly provide full Live2D Cubism Core runtime binaries for ready use. You must download and place them manually.

Please follow these steps:

1. Download the latest **CubismSdkForNative** from the official Live2D website  
   Download page: https://www.live2d.com/en/sdk/about/  
   Current latest version: **CubismSdkForNative-5-r.4.1**
2. Open the SDK package and locate the `Core` directory
3. Copy the `Core` directory into this project's `sdk/` directory
4. Rename the copied directory to: `cubism`

Expected result:

- `sdk/cubism/include/...`
- `sdk/cubism/lib/...`

> If this structure is incorrect, CMake linkage for Live2D Core will fail.

### 3. Build & Run

#### macOS

Typical workflow:

1. Configure CMake
2. Build targets
3. Launch generated `AmaiGirl.app`

You may also build directly with VS Code + CMake Tools.

CLI build examples (Ninja):

- Debug

```bash
cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j
./build-debug/AmaiGirl.app/Contents/MacOS/AmaiGirl
```

- Release

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
./build-release/AmaiGirl.app/Contents/MacOS/AmaiGirl
```

> If you prefer using the existing `build` directory, reconfigure it first and then rebuild.

#### Windows

- Standard build:

```powershell
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
.\build-release\AmaiGirl.exe
```

- Portable package:

```powershell
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target package_windows -j
```

- Deployment notes:
   - `deploy_windows`: deploys Qt runtime files next to the executable
   - `package_windows`: creates a portable directory and outputs `AmaiGirl-windows.zip`
   - `windeployqt` is discovered from `PATH` by default
   - Override explicitly with `AMAIGIRL_WINDEPLOYQT_EXECUTABLE` if needed

#### Linux

- Standard build:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/AmaiGirl
```

- AppImage packaging:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target package_appimage -j
./build/AmaiGirl-x86_64.AppImage
```

- Portable/CI recommendation: avoid machine-specific paths; use `PATH` discovery first. If needed, override via CMake variables:
   - `AMAIGIRL_LINUXDEPLOY_EXECUTABLE`
   - `AMAIGIRL_APPIMAGETOOL_EXECUTABLE`
   - `AMAIGIRL_QMAKE_EXECUTABLE`

Example:

```bash
cmake -S . -B build -G Ninja \
   -DAMAIGIRL_LINUXDEPLOY_EXECUTABLE=/opt/tools/linuxdeploy-x86_64.AppImage \
   -DAMAIGIRL_APPIMAGETOOL_EXECUTABLE=/opt/tools/appimagetool-x86_64.AppImage \
   -DAMAIGIRL_QMAKE_EXECUTABLE=/opt/Qt/6.9.3/gcc_64/bin/qmake6
```

### 4. Coding & Commit Guidelines

- Do not develop directly on `dev`; branch from `dev` into feature branches: `feat/xxx` (for example `feat/windows/audio`, `feat/model-sync`)
- Avoid committing development work directly on `main`
- Keep changes focused and minimal
- Avoid mixing unrelated refactors in one PR
- Follow existing project style (naming, formatting, file layout)
- Cross-platform macro rules:
   - Windows: only `#if defined(Q_OS_WIN32)` / `#elif defined(Q_OS_WIN32)`
   - Linux: only `#if defined(Q_OS_LINUX)` / `#elif defined(Q_OS_LINUX)`
   - macOS: only `#if defined(Q_OS_MACOS)` / `#elif defined(Q_OS_MACOS)`
   - Globally disallow direct platform checks via `#ifdef` / `#ifndef`
- Sync i18n (`res/i18n/*.ts`) when UI texts are changed
- Update `NOTICE` / `THIRD_PARTY_LICENSES.md` / `THIRD_PARTY_LICENSES.en.md` when distribution/license-related content changes

Run checks locally before pushing:

```bash
python3 scripts/check_platform_macro_style.py --root src --platform windows
python3 scripts/check_platform_macro_style.py --root src --platform linux
python3 scripts/check_platform_macro_style.py --root src --platform macos
python3 scripts/check_platform_macro_style.py --root src --platform all
```

CI policy:

- All `feat/*` branches run macro-style checks
- `feat/windows*` branches run Windows macro checks
- `feat/linux*` branches run Linux macro checks
- `feat/macos*` branches run macOS macro checks
- Other `feat/xxx` branches (cross-platform features) run `--platform all`
- PRs targeting `dev` must come from `feat/*`
- PRs from `feat/windows*` / `feat/linux*` / `feat/macos*` to `dev` also run a "diff scope guard": C/C++ changes must stay inside the corresponding platform macro-guarded regions to avoid accidental shared-code edits
- If exceptions are needed, add path-glob entries (one per line) to `.github/platform-diff-allowlist.txt` for the platform branch diff-scope guard

Recommended repository protection settings:

- Block direct pushes to `dev`
- Require pull requests and passing CI checks before merging into `dev`

### 5. Theme Extension Rules

- Config semantics: use `theme` as the single theme key. This key is persisted by `SettingsManager` in runtime user configuration (for example, `Configs/config.json` under the user data directory), not in a versioned repository file; do not add or reintroduce `themeMode`
- Theme ID registration:
   - Register new IDs in `Theme::availableThemeIds()`
   - Extend normalization/compat logic in `Theme::normalizeThemeId()` (and underlying implementation)
   - If the project default theme changes, also update the default in `SettingsManager`
- Theme apply dispatch:
   - Add the new theme branch in `src/ui/theme/ThemeApi.cpp` for `installApplicationStyle()` / `applyTheme()`
   - Keep runtime hot switching working from the settings window (no restart required)
- SVG icon rules:
   - Theme-specific SVG files must live under `res/icons/<theme-id>/`, for example `res/icons/era-style/`
   - Every `Theme::IconToken` entry must have a mapping in `Theme::iconRelativePath()`
   - Do not place theme-specific SVG files in the root `res/icons/` directory
- Business-layer dependency rule:
   - UI windows such as `ChatWindow` and `SettingsWindow` should depend only on the theme abstraction layer (`src/ui/theme/ThemeApi.hpp` and `src/ui/theme/ThemeWidgets.hpp`)
   - Do not directly `#include` concrete theme implementation directories (for example, `ui/era-style/*`) in business window code; expose new theme capabilities through the abstraction layer first
- Validation checklist for any new theme:
   - Theme appears in the "Current Theme" dropdown and can be selected
   - Colors and icons update immediately after switching (hot switch)
   - Theme persists across app restart
   - Missing single-icon resources have fallback behavior and do not crash the app

### 6. Pull Request Checklist

Please include:

- Purpose and context
- Key implementation details
- Local verification steps
- Screenshots/recordings if UI behavior changed

### 7. Security & Compliance Notes

- Never commit secrets (API keys, tokens)
- Model assets may have separate licenses; verify before submitting
- Do not package restricted third-party assets as if they are freely redistributable
