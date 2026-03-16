# 贡献指南

<p><a href="CONTRIBUTING.md#zh-cn">简体中文</a> | <a href="CONTRIBUTING.en.md#en-us">English</a></p>

<a id="zh-cn"></a>

## 简体中文

感谢你对 AmaiGirl 的关注与贡献！

本项目目标是打造全平台原生的 AI 桌面助手。当前实现了 macOS、Windows 和 Linux 平台的版本。

### 1. 开发前准备

#### macOS

- 构建环境：Xcode 15（macOS 14 SDK）或更高版本
- 编译工具：CMake + Ninja + Clang
- Qt：Qt 6（项目当前使用 Core / Gui / Widgets / OpenGL / OpenGLWidgets / Network / Multimedia / Svg）

#### Windows

- 建议环境：Windows 10/11（x86_64）
- 编译工具：Visual Studio 2022（MSVC v143）或更新版本，或在对应 MSVC 开发者环境中使用 CMake + Ninja
- 当前不支持 MinGW/GCC：仓库使用的 Live2D Cubism Windows 库为 MSVC 产物
- Qt：Qt 6（Core / Gui / Widgets / OpenGL / OpenGLWidgets / Network / Multimedia / Svg），并确保 `windeployqt` 可用
- Live2D：请确认 `sdk/cubism/lib/windows/x86_64/<toolset>/` 下存在对应 MSVC 工具集的 `Live2DCubismCore_MD.lib` / `Live2DCubismCore_MDd.lib`

#### Linux

- 建议环境：Wayland 会话（X11 可作为回退后端）
- 构建工具：CMake + Ninja + GCC/Clang
- Qt：Qt 6（Core / Gui / Widgets / OpenGL / OpenGLWidgets / Network / Multimedia / Svg）
- 可选打包工具（AppImage）：`linuxdeploy`、`appimagetool`

### 2. **Live2D SDK（重要，必须自行下载）**

由于授权原因，仓库不会直接提供可完整使用的 Live2D Cubism Core 二进制内容。你需要自行从官网获取并放置。

请按以下步骤操作：

1. 前往 Live2D 官网下载最新 **CubismSdkForNative**  
   下载地址：https://www.live2d.com/zh-CHS/sdk/about/  
   当前最新版本：**CubismSdkForNative-5-r.4.1**
2. 解压后进入该 SDK 的 `Core` 目录
3. 将 `Core` 目录复制到本项目 `sdk/` 下
4. 将复制后的目录重命名为：`cubism`

最终目录应类似：

- `sdk/cubism/include/...`
- `sdk/cubism/lib/...`

> 若目录结构不正确，CMake 链接 Live2D Core 时会失败。

### 3. 构建与运行

#### macOS

常规流程：

1. 配置 CMake
2. 执行构建
3. 运行生成的 `AmaiGirl.app`

你也可以使用 VS Code + CMake Tools 直接构建。

命令行构建示例（Ninja）：

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

> 若你使用的是现有 `build` 目录，也可先在该目录重新配置后再构建。

#### Windows

- 常规构建：

```powershell
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
.\build-release\AmaiGirl.exe
```

- 便携包打包：

```powershell
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target package_windows -j
```

- 部署说明：
   - `deploy_windows`：将 Qt 运行时部署到可执行文件目录
   - `package_windows`：生成便携目录并输出 `AmaiGirl-windows.zip`
   - 默认通过 `PATH` 查找 `windeployqt`
   - 如需显式指定路径，可使用 `AMAIGIRL_WINDEPLOYQT_EXECUTABLE`

#### Linux

- 常规构建：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/AmaiGirl
```

- AppImage 打包：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target package_appimage -j
./build/AmaiGirl-x86_64.AppImage
```

- 跨环境/CI 推荐：避免写死本地路径，优先通过 `PATH` 找工具；必要时使用以下 CMake 变量覆盖：
   - `AMAIGIRL_LINUXDEPLOY_EXECUTABLE`
   - `AMAIGIRL_APPIMAGETOOL_EXECUTABLE`
   - `AMAIGIRL_QMAKE_EXECUTABLE`

示例：

```bash
cmake -S . -B build -G Ninja \
   -DAMAIGIRL_LINUXDEPLOY_EXECUTABLE=/opt/tools/linuxdeploy-x86_64.AppImage \
   -DAMAIGIRL_APPIMAGETOOL_EXECUTABLE=/opt/tools/appimagetool-x86_64.AppImage \
   -DAMAIGIRL_QMAKE_EXECUTABLE=/opt/Qt/6.9.3/gcc_64/bin/qmake6
```

### 4. 代码与提交规范

- 不要直接在 `dev` 上开发；请从 `dev` 派生功能分支进行开发：`feat/xxx`（例如 `feat/windows/audio`、`feat/model-sync`）
- 不建议直接在 `main` 分支上进行开发提交
- 尽量保持改动聚焦、最小化
- 不要在同一 PR 中混入无关重构
- 保持现有代码风格（命名、缩进、文件组织）
- 跨平台宏统一规范：
   - Windows：仅允许 `#if defined(Q_OS_WIN32)` / `#elif defined(Q_OS_WIN32)`
   - Linux：仅允许 `#if defined(Q_OS_LINUX)` / `#elif defined(Q_OS_LINUX)`
   - macOS：仅允许 `#if defined(Q_OS_MACOS)` / `#elif defined(Q_OS_MACOS)`
   - 统一禁止：`#ifdef` / `#ifndef` 直接判断平台宏
- 对 UI 文案改动，请同步 i18n（`res/i18n/*.ts`）
- 涉及许可证与分发内容，请同步更新 `NOTICE` / `THIRD_PARTY_LICENSES.md` / `THIRD_PARTY_LICENSES.en.md`

可在本地执行以下命令进行检查：

```bash
python3 scripts/check_platform_macro_style.py --root src --platform windows
python3 scripts/check_platform_macro_style.py --root src --platform linux
python3 scripts/check_platform_macro_style.py --root src --platform macos
python3 scripts/check_platform_macro_style.py --root src --platform all
```

CI 规则：

- 所有 `feat/*` 分支都会执行宏规范检查
- `feat/windows*` 分支自动执行 Windows 宏规范检查
- `feat/linux*` 分支自动执行 Linux 宏规范检查
- `feat/macos*` 分支自动执行 macOS 宏规范检查
- 其他 `feat/xxx`（全平台特性）自动执行 `--platform all` 全量检查
- 目标分支为 `dev` 的 PR，来源分支必须是 `feat/*`
- 对 `feat/windows*` / `feat/linux*` / `feat/macos*` 到 `dev` 的 PR，会额外执行“改动范围守卫”：C/C++ 改动必须落在对应平台宏保护块内，避免误改共享代码
- 若确需例外文件，可在 `.github/platform-diff-allowlist.txt` 中按行添加路径 glob 白名单（仅用于平台分支改动范围守卫）

推荐在仓库设置中开启分支保护：

- 禁止直接 push 到 `dev`
- 要求 PR 合并并通过 CI 检查后才能进入 `dev`

### 5. 主题扩展规范（Theme）

- 配置语义：主题配置键统一使用 `theme`，由 `SettingsManager` 读写到运行时用户配置（例如用户目录下的 `Configs/config.json`）；不要新增或回退到 `themeMode`
- 主题 ID 注册：
   - 在 `Theme::availableThemeIds()` 中注册新主题 ID
   - 在 `Theme::normalizeThemeId()`（及其底层实现）中补充规范化与兼容逻辑
   - 若需变更默认主题，再同步调整 `SettingsManager` 的默认值
- 主题应用分发：
   - 在 `src/ui/theme/ThemeApi.cpp` 的 `installApplicationStyle()` / `applyTheme()` 中增加新主题分支
   - 保持设置页切换后可热更新（无需重启）
- SVG 图标规范：
   - 主题独有 SVG 必须放在 `res/icons/<theme-id>/`，例如 `res/icons/era-style/`
   - `Theme::IconToken` 的每个条目都必须在 `Theme::iconRelativePath()` 中有映射
   - 不要将主题专属 SVG 继续放在 `res/icons/` 根目录
- 业务层依赖约束：
   - `ChatWindow`、`SettingsWindow` 等业务窗口应仅依赖主题抽象层（`src/ui/theme/ThemeApi.hpp` 与 `src/ui/theme/ThemeWidgets.hpp`）
   - 禁止在业务窗口中直接 `#include` 具体主题实现目录（例如 `ui/era-style/*`）；新增主题能力应先经主题抽象层暴露
- 验证清单（新增主题必做）：
   - 在“当前主题”下拉中可见并可切换
   - 切换主题后颜色与图标即时生效（热切换）
   - 重启应用后主题保持为上次选择
   - 缺失单个图标资源时有回退策略，且应用不崩溃

### 6. Pull Request 建议

提交 PR 时建议包含：

- 变更目的与背景
- 关键改动说明
- 本地验证方式（如何复现/验证）
- 若有 UI 变化，附截图或录屏

### 7. 安全与合规注意事项

- 严禁提交私密密钥（API Key、Token）
- 模型素材可能受单独条款约束，提交前请确认许可
- 不得将第三方受限内容以“默认可分发”方式打包进仓库
