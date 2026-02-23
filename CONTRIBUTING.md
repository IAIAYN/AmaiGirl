# 贡献指南

<p><a href="CONTRIBUTING.md#zh-cn">简体中文</a> | <a href="CONTRIBUTING.en.md#en-us">English</a></p>

<a id="zh-cn"></a>

## 简体中文

感谢你对 AmaiGirl 的关注与贡献！

本项目目标是打造全平台 AI 桌面助手。当前以 macOS 为首个实现版本，后续会持续扩展到更多平台。

### 1. 开发前准备

- 推荐系统：macOS 14.0+（当前主开发平台）
- 编译工具：CMake + Ninja + Clang
- Qt：Qt 6（项目当前使用 Core / Gui / Widgets / OpenGL / OpenGLWidgets / Network / Multimedia）

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

### 4. 代码与提交规范

- 请在 `dev` 分支上开发，或从 `dev` 分支创建功能分支（例如 `feature/xxx`）
- 不建议直接在 `main` 分支上进行开发提交
- 尽量保持改动聚焦、最小化
- 不要在同一 PR 中混入无关重构
- 保持现有代码风格（命名、缩进、文件组织）
- 对 UI 文案改动，请同步 i18n（`res/i18n/*.ts`）
- 涉及许可证与分发内容，请同步更新 `NOTICE` / `THIRD_PARTY_LICENSES.md` / `THIRD_PARTY_LICENSES.en.md`

### 5. Pull Request 建议

提交 PR 时建议包含：

- 变更目的与背景
- 关键改动说明
- 本地验证方式（如何复现/验证）
- 若有 UI 变化，附截图或录屏

### 6. 安全与合规注意事项

- 严禁提交私密密钥（API Key、Token）
- 模型素材可能受单独条款约束，提交前请确认许可
- 不得将第三方受限内容以“默认可分发”方式打包进仓库
