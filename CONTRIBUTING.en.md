# Contributing Guide

<p><a href="CONTRIBUTING.md#zh-cn">简体中文</a> | <a href="CONTRIBUTING.en.md#en-us">English</a></p>

<a id="en-us"></a>

## English

Thank you for your interest in contributing to AmaiGirl.

AmaiGirl aims to become a cross-platform AI desktop assistant. The current implementation focuses on macOS first, with more platforms planned.

### 1. Prerequisites

#### macOS

- Build environment: Xcode 15 (macOS 14 SDK) or newer
- Build tools: CMake + Ninja + Clang
- Qt: Qt 6 (Core / Gui / Widgets / OpenGL / OpenGLWidgets / Network / Multimedia)

#### Windows

- TBD

#### Linux

- TBD

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

- TBD

#### Linux

- TBD

### 4. Coding & Commit Guidelines

- Develop on the `dev` branch, or create your feature branch from `dev` (e.g. `feature/xxx`)
- Avoid committing development work directly on `main`
- Keep changes focused and minimal
- Avoid mixing unrelated refactors in one PR
- Follow existing project style (naming, formatting, file layout)
- Sync i18n (`res/i18n/*.ts`) when UI texts are changed
- Update `NOTICE` / `THIRD_PARTY_LICENSES.md` / `THIRD_PARTY_LICENSES.en.md` when distribution/license-related content changes

### 5. Pull Request Checklist

Please include:

- Purpose and context
- Key implementation details
- Local verification steps
- Screenshots/recordings if UI behavior changed

### 6. Security & Compliance Notes

- Never commit secrets (API keys, tokens)
- Model assets may have separate licenses; verify before submitting
- Do not package restricted third-party assets as if they are freely redistributable
