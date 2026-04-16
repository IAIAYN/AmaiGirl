# Agent 开发指南

<p><a href="AGENT.md#zh-cn">简体中文</a> | <a href="AGENT.en.md#en-us">English</a></p>

<a id="zh-cn"></a>

## 简体中文

本文档面向希望为 AmaiGirl 当前 Agent 能力开发、重构、排错和扩展功能的贡献者。内容覆盖当前 Agent 的框架、技术栈、目录职责、关键路径、配置与持久化、扩展入口、开发规范与验证建议。

在修改 Agent 相关代码前，建议先结合以下文档一起阅读：

- [贡献指南](CONTRIBUTING.md)
- [英文版 Agent 指南](AGENT.en.md)

### 目录

- [1. 当前 Agent 的定位](#agent-positioning)
- [2. 当前 Agent 框架概览](#agent-architecture)
- [3. 技术栈](#tech-stack)
- [4. 运行链路](#runtime-flow)
- [5. 目录组织与职责](#module-layout)
- [6. 关键路径与索引](#key-paths)
- [7. 配置、状态与持久化](#config-and-persistence)
- [8. 常见开发任务指南](#development-playbook)
- [9. 开发规范](#development-rules)
- [10. 调试与验证建议](#debug-and-validation)
- [11. 当前边界与演进方向](#known-boundaries)

<a id="agent-positioning"></a>

### 1. 当前 Agent 的定位

当前实现并不是一个独立的“多进程 Agent 平台”，而是一个嵌入在 Qt 桌面应用中的 Agent Runtime 体系，核心目标是：

- 将聊天、工具调用、TTS、播放与 UI 呈现整合到同一条运行链路中。
- 通过 OpenAI 兼容接口完成 LLM 与 TTS 请求。
- 通过 MCP（Model Context Protocol）把外部工具暴露给模型。
- 在不中断桌宠主渲染循环的情况下，保持聊天状态、工具调用状态和 UI 状态一致。

当前 Agent 的主干是：

- ChatController 负责编排。
- AgentRuntime 负责对话状态机与持久化。
- OpenAIChatClient 与 OpenAITtsClient 负责 Provider 接入。
- McpStdioAdapter 与 McpHttpSseAdapter 负责工具侧接入。
- ChatWindow / SettingsWindow 负责交互入口与状态展示。

补充说明：

- Runtime 已经是默认且唯一启用路径，`SettingsManager::useAgentRuntime()` 仅为兼容保留，逻辑上始终返回 true。
- `ISkillRegistry` 目前只有占位接口，当前仓库尚未形成独立 Skill 子系统；如要扩展，请优先围绕 Runtime、Provider、MCP、UI 四条主线设计。

<a id="agent-architecture"></a>

### 2. 当前 Agent 框架概览

#### 2.1 分层结构

当前 Agent 可按以下层次理解：

1. 应用装配层
   - [src/app/main.cpp](src/app/main.cpp)
   - 创建 `SettingsWindow`、`ChatWindow`、`ChatController`、`Renderer`，并负责信号连接。
2. 编排层
   - [src/ai/ChatController.hpp](src/ai/ChatController.hpp)
   - [src/ai/ChatController.cpp](src/ai/ChatController.cpp)
   - 负责模型切换、发送消息、刷新 MCP Tools、路由 Tool Call、协调 TTS/播放/UI busy 状态。
3. Runtime 层
   - [src/ai/AgentRuntime.hpp](src/ai/AgentRuntime.hpp)
   - [src/ai/AgentRuntime.cpp](src/ai/AgentRuntime.cpp)
   - 负责对话状态机、消息缓冲、占位 assistant message、tool result 回填、聊天持久化过滤。
4. Provider 层
   - Chat: [src/ai/OpenAIChatClient.hpp](src/ai/OpenAIChatClient.hpp)、[src/ai/OpenAIChatClient.cpp](src/ai/OpenAIChatClient.cpp)
   - TTS: [src/ai/OpenAITtsClient.hpp](src/ai/OpenAITtsClient.hpp)、[src/ai/OpenAITtsClient.cpp](src/ai/OpenAITtsClient.cpp)
   - 负责与 OpenAI 兼容端点通信。
5. Tool/MCP 接入层
   - [src/ai/core/IMcpAdapter.hpp](src/ai/core/IMcpAdapter.hpp)
   - [src/ai/McpStdioAdapter.hpp](src/ai/McpStdioAdapter.hpp)
   - [src/ai/McpHttpSseAdapter.hpp](src/ai/McpHttpSseAdapter.hpp)
   - [src/ai/core/ToolRegistry.hpp](src/ai/core/ToolRegistry.hpp)
   - 负责发现 tools、命名路由、执行 tool call。
6. 配置与持久化层
   - [src/common/SettingsManager.hpp](src/common/SettingsManager.hpp)
   - [src/ai/ConversationRepository.hpp](src/ai/ConversationRepository.hpp)
   - [src/ai/ConversationRepositoryAdapter.hpp](src/ai/ConversationRepositoryAdapter.hpp)
   - 负责 config.json、mcp.json、聊天记录与缓存目录。
7. UI 展示层
   - [src/ui/ChatWindow.hpp](src/ui/ChatWindow.hpp)
   - [src/ui/SettingsWindow.hpp](src/ui/SettingsWindow.hpp)
   - [src/ui/McpServerEditorDialog.hpp](src/ui/McpServerEditorDialog.hpp)
   - 负责输入、状态展示、MCP 开关与配置编辑。

#### 2.2 设计原则

- ChatController 是当前 Agent 入口编排者，不要把跨模块流程拆散到多个 UI 类中。
- AgentRuntime 是对话状态与聊天持久化的单一事实来源；启用 Runtime 时，ChatWindow 只做展示，不直接写聊天 JSON。
- Provider 层与 MCP Adapter 层通过接口隔离，便于未来替换为其他供应商或协议。
- MCP Tool 对模型暴露的名称与服务端原始工具名解耦，通过 ToolRegistry 做映射。
- 设置变更先落到 SettingsManager，再由信号触发 Runtime/Controller 刷新，不要直接在业务代码里读写裸 JSON。

<a id="tech-stack"></a>

### 3. 技术栈

- 语言标准：C++20
- UI 与基础设施：Qt 6
  - Core
  - Gui
  - Widgets
  - Network
  - Multimedia
  - OpenGL / OpenGLWidgets
- 构建系统：CMake + Ninja
- 大模型接口：OpenAI 兼容 Chat Completions
- TTS 接口：OpenAI 兼容 Audio Speech
- 工具协议：MCP
  - Stdio
  - HTTP/SSE
- 持久化格式：JSON
- 并发模型：Qt 事件循环 + 信号/槽 + `QThread::create()` 后台线程

对 Agent 子系统最重要的 Qt 组件包括：

- `QNetworkAccessManager` / `QNetworkReply`
- `QJsonObject` / `QJsonArray` / `QJsonDocument`
- `QThread`
- `QMediaPlayer` / `QAudioOutput`
- `QObject` 信号/槽机制

<a id="runtime-flow"></a>

### 4. 运行链路

#### 4.1 启动与装配

应用启动后，装配关系位于 [src/app/main.cpp](src/app/main.cpp)：

1. 创建 `SettingsWindow`。
2. 创建 `ChatWindow`。
3. 创建 `ChatController`。
4. 将 `ChatWindow`、`Renderer` 交给 `ChatController`。
5. 将 `SettingsWindow::mcpSettingsChanged` 连接到 `ChatController::markMcpToolsDirty()`。
6. 模型切换时，通过 `ChatController::onModelChanged()` 更新当前模型上下文。

#### 4.2 发送消息链路

正常发送消息的主链路如下：

1. `ChatWindow` 发出发送信号。
2. `ChatController` 接收并准备当前模型上下文、MCP tools 与 UI busy 状态。
3. `AgentRuntime::submitUserMessage()` 载入历史消息、追加 user message、插入 assistant placeholder。
4. `AgentRuntime` 从 `SettingsManager` 刷新 Chat 配置并调用 `IChatProvider::startChat()`。
5. `OpenAIChatClient` 发起请求，流式 token 通过信号回传。
6. `AgentRuntime` 维护状态机，并根据是否启用 TTS 决定直接完成、先 TTS、或进入 tool call 阶段。

#### 4.3 Tool Call 链路

当模型请求工具调用时，当前实现的闭环是：

1. `OpenAIChatClient` 解析 provider 返回的 `tool_calls`。
2. `OpenAIChatClient` 发出 `toolCallRequested(toolName, toolInput, toolCallId)`。
3. `AgentRuntime::onClientToolCallRequested()`：
   - 移除空 assistant placeholder。
   - 先把 assistant tool_calls 消息以协议格式写入内存消息数组。
   - 标记 `m_waitingToolResult = true`。
   - 向外发出 `toolCallRequested(toolName, toolInput)`。
4. `ChatController::onRuntimeToolCallRequested()`：
   - 通过 `ToolRegistry` 将 exposed tool name 还原成 `serverName + rawToolName`。
   - 如果注册表未命中，会退回启发式命名解析。
   - 在后台线程中创建对应 `IMcpAdapter` 并执行 `callTool()`。
5. `ChatController` 将结果通过 `AgentRuntime::submitToolResult()` 回填。
6. `AgentRuntime` 追加 tool message，再次调用 Chat Provider 继续对话。

关键约束：

- Tool result message 只用于协议上下文，不会直接持久化到用户可见聊天历史中。
- Runtime 会过滤空 assistant message 和纯协议 tool payload，避免污染聊天记录。

#### 4.4 MCP Tools 刷新链路

MCP tools 的刷新逻辑集中在 `ChatController::reloadMcpTools()`：

1. 读取 `SettingsManager::mcpServers()`。
2. 基于配置缓存、原始 tool 缓存、状态缓存判断哪些 server 需要重新加载。
3. 对未变化且已有缓存的 server 复用结果。
4. 对变化的 server，在后台线程中创建 adapter 并调用 `listTools()`。
5. 将不同 server 的 tools 合并为 OpenAI function tools。
6. 用 `ToolRegistry` 维护 exposed tool name 与原始路由的映射。
7. 调用 `OpenAIChatClient::setTools()` 更新 provider tools 列表。
8. 同步 MCP server 状态到 ChatWindow 弹层。

#### 4.5 TTS 与播放链路

1. `AgentRuntime` 在文本完成后，根据 `ttsBaseUrl` 是否为空决定是否启用 TTS。
2. 启用时，发出 `requestStartTts()` 给 `ChatController`。
3. `ChatController` 调用 `OpenAITtsClient::startSpeech()` 生成音频文件。
4. 生成成功后：
   - 播放音频。
   - 触发 paced text reveal，与播放进度保持大致同步。
   - 将最终 assistant 文本写回持久化。

<a id="module-layout"></a>

### 5. 目录组织与职责

#### 5.1 Agent 相关主目录

- [src/ai](src/ai)
  - Agent 运行期实现、Provider、MCP Adapter、聊天存储适配。
- [src/ai/core](src/ai/core)
  - Agent 核心抽象接口、MCP 配置结构、ToolRegistry、状态定义。
- [src/common](src/common)
  - 全局设置与路径管理。
- [src/ui](src/ui)
  - 聊天窗口、设置窗口、MCP 配置对话框与相关 UI 控件。
- [src/app](src/app)
  - 应用装配、窗口初始化、信号连接。

#### 5.2 src/ai 文件职责索引

- [src/ai/ChatController.cpp](src/ai/ChatController.cpp)
  - Agent 总编排。
  - MCP tools 发现、缓存、状态发布。
  - Tool call 实际执行。
  - TTS 与播放串联。
- [src/ai/AgentRuntime.cpp](src/ai/AgentRuntime.cpp)
  - Runtime 状态机。
  - 消息缓冲与持久化过滤。
  - tool result 回填后继续对话。
- [src/ai/OpenAIChatClient.cpp](src/ai/OpenAIChatClient.cpp)
  - OpenAI 兼容聊天请求。
  - SSE token 解析。
  - tool call 解析与转发。
- [src/ai/OpenAITtsClient.cpp](src/ai/OpenAITtsClient.cpp)
  - OpenAI 兼容 TTS 请求。
  - 输出音频落地与转码兼容处理。
- [src/ai/McpStdioAdapter.cpp](src/ai/McpStdioAdapter.cpp)
  - 通过进程 stdio 与 MCP server 通信。
- [src/ai/McpHttpSseAdapter.cpp](src/ai/McpHttpSseAdapter.cpp)
  - 通过 HTTP/SSE 与 MCP server 通信。
- [src/ai/ConversationRepository.cpp](src/ai/ConversationRepository.cpp)
  - 每个模型维度的聊天消息读写。

#### 5.3 src/ai/core 文件职责索引

- [src/ai/core/IAgentRuntime.hpp](src/ai/core/IAgentRuntime.hpp)
  - Runtime 对外抽象与状态信号。
- [src/ai/core/IChatProvider.hpp](src/ai/core/IChatProvider.hpp)
  - Chat Provider 接口。
- [src/ai/core/ITtsProvider.hpp](src/ai/core/ITtsProvider.hpp)
  - TTS Provider 接口。
- [src/ai/core/IMcpAdapter.hpp](src/ai/core/IMcpAdapter.hpp)
  - MCP adapter 抽象与工厂入口。
- [src/ai/core/IMcpHttpSseAdapter.hpp](src/ai/core/IMcpHttpSseAdapter.hpp)
  - HTTP/SSE adapter 专用接口扩展。
- [src/ai/core/ToolRegistry.hpp](src/ai/core/ToolRegistry.hpp)
  - exposed tool name 到实际路由的映射表。
- [src/ai/core/McpServerConfig.hpp](src/ai/core/McpServerConfig.hpp)
  - MCP server 配置结构、序列化与校验。
- [src/ai/core/McpServerStatus.hpp](src/ai/core/McpServerStatus.hpp)
  - MCP server UI 状态模型。
- [src/ai/core/ISkillRegistry.hpp](src/ai/core/ISkillRegistry.hpp)
  - 预留接口，目前未承载实际逻辑。

<a id="key-paths"></a>

### 6. 关键路径与索引

以下索引按“改什么功能，就先看哪些文件”组织。

| 任务 | 优先查看路径 |
| --- | --- |
| 修改 Agent 主流程 | [src/ai/ChatController.cpp](src/ai/ChatController.cpp), [src/ai/AgentRuntime.cpp](src/ai/AgentRuntime.cpp) |
| 新增/替换 Chat Provider | [src/ai/core/IChatProvider.hpp](src/ai/core/IChatProvider.hpp), [src/ai/OpenAIChatClient.cpp](src/ai/OpenAIChatClient.cpp), [src/ai/ChatController.cpp](src/ai/ChatController.cpp) |
| 新增/替换 TTS Provider | [src/ai/core/ITtsProvider.hpp](src/ai/core/ITtsProvider.hpp), [src/ai/OpenAITtsClient.cpp](src/ai/OpenAITtsClient.cpp), [src/ai/ChatController.cpp](src/ai/ChatController.cpp) |
| 新增 MCP 接入类型 | [src/ai/core/IMcpAdapter.hpp](src/ai/core/IMcpAdapter.hpp), [src/ai/core/McpServerConfig.hpp](src/ai/core/McpServerConfig.hpp), [src/ui/McpServerEditorDialog.cpp](src/ui/McpServerEditorDialog.cpp) |
| 修改 MCP tools 刷新与缓存 | [src/ai/ChatController.cpp](src/ai/ChatController.cpp), [src/ai/core/ToolRegistry.cpp](src/ai/core/ToolRegistry.cpp) |
| 修改 tool name 暴露规则 | [src/ai/ChatController.cpp](src/ai/ChatController.cpp), [src/ai/core/ToolRegistry.hpp](src/ai/core/ToolRegistry.hpp) |
| 修改聊天记录落盘逻辑 | [src/ai/AgentRuntime.cpp](src/ai/AgentRuntime.cpp), [src/ai/ConversationRepository.cpp](src/ai/ConversationRepository.cpp) |
| 修改 Agent 设置项 | [src/common/SettingsManager.hpp](src/common/SettingsManager.hpp), [src/common/SettingsManager.cpp](src/common/SettingsManager.cpp), [src/ui/SettingsWindow.cpp](src/ui/SettingsWindow.cpp) |
| 修改 MCP 设置 UI | [src/ui/SettingsWindow.cpp](src/ui/SettingsWindow.cpp), [src/ui/McpServerCard.cpp](src/ui/McpServerCard.cpp), [src/ui/McpServerEditorDialog.cpp](src/ui/McpServerEditorDialog.cpp) |
| 修改聊天窗口里的 MCP 状态弹层 | [src/ui/ChatWindow.cpp](src/ui/ChatWindow.cpp), [src/ai/core/McpServerStatus.hpp](src/ai/core/McpServerStatus.hpp), [src/ai/ChatController.cpp](src/ai/ChatController.cpp) |
| 修改应用装配与信号连接 | [src/app/main.cpp](src/app/main.cpp) |

<a id="config-and-persistence"></a>

### 7. 配置、状态与持久化

#### 7.1 配置来源

当前 Agent 相关配置由 `SettingsManager` 统一管理：

- Chat 配置
  - `aiBaseUrl`
  - `aiApiKey`
  - `aiModel`
  - `aiSystemPrompt`
  - `aiStreamEnabled`
- TTS 配置
  - `ttsBaseUrl`
  - `ttsApiKey`
  - `ttsModel`
  - `ttsVoice`
- MCP 配置
  - `mcpServers()`
  - `setMcpServers()`
  - `addMcpServer()`
  - `updateMcpServer()`
  - `removeMcpServer()`

#### 7.2 JSON 文件分工

- `config.json`
  - 全局 AI/TTS/UI 等配置。
- `mcp.json`
  - MCP server 列表，不再与一般设置混写。
- `*.chat.json`
  - 按模型持久化聊天记录。

注意：

- 不要新增绕过 `SettingsManager` 的裸文件读写逻辑。
- 若调整字段名或迁移结构，必须同时考虑旧版本兼容和迁移路径。

#### 7.3 聊天记录的可见性规则

`AgentRuntime::visibleMessagesForPersistence()` 当前会过滤：

- `role == tool` 的协议消息。
- 空 assistant placeholder。
- 仅用于协议合法性的 assistant tool_call envelope。

这意味着：

- 用户聊天历史保持干净。
- Provider 所需的协议消息仍保留在运行时内存上下文中。
- 如果你修改该规则，必须同时验证聊天展示、继续对话、tool call 回填三者没有被破坏。

<a id="development-playbook"></a>

### 8. 常见开发任务指南

#### 8.1 新增一个 Chat Provider

建议步骤：

1. 在 [src/ai/core/IChatProvider.hpp](src/ai/core/IChatProvider.hpp) 范围内确认接口是否足够。
2. 新增 provider 实现文件，行为对齐 `OpenAIChatClient`。
3. 明确以下能力是否支持：
   - stream token
   - tool call
   - cancel
4. 在 [src/ai/ChatController.cpp](src/ai/ChatController.cpp) 的装配阶段接入新 provider。
5. 验证 AgentRuntime 在非 OpenAI provider 下仍能正确处理 tool call 和 error。

不建议：

- 让 `AgentRuntime` 直接依赖具体 provider 类型。
- 在 UI 中直接发网络请求。

#### 8.2 新增一个 TTS Provider

建议步骤：

1. 遵循 [src/ai/core/ITtsProvider.hpp](src/ai/core/ITtsProvider.hpp) 接口。
2. 保持输出语音文件路径由调用方提供。
3. 明确失败时是否有可理解错误文本。
4. 验证 `ChatController` 的播放与 lip sync 逻辑不依赖某种私有音频格式。

#### 8.3 新增一种 MCP Adapter

当前仓库已有两种接入：

- Stdio
- HTTP/SSE

新增接入类型时至少要同步修改：

1. [src/ai/core/McpServerConfig.hpp](src/ai/core/McpServerConfig.hpp)
   - 增加 `Type` 枚举与序列化逻辑。
2. [src/ai/core/IMcpAdapter.cpp](src/ai/core/IMcpAdapter.cpp)
   - 工厂函数中加入分派。
3. 新 adapter 实现文件。
4. [src/ui/McpServerEditorDialog.cpp](src/ui/McpServerEditorDialog.cpp)
   - 增加配置编辑 UI。
5. 必要时更新 MCP 配置校验与测试连接逻辑。

#### 8.4 修改 Tool 暴露或路由规则

当前 exposed tool name 规则由 `ChatController` 内部工具函数生成，典型形式为：

- `mcp_<server>__<tool>`

修改时必须同时考虑：

- 名称合法性
- 长度截断策略
- 冲突去重
- ToolRegistry 反查是否稳定
- 历史消息中 tool call 的兼容性

如果只是想改展示名而不是协议名，优先在 UI 层处理，不要轻易改 provider 可见名称规则。

#### 8.5 新增 Agent 设置项

推荐顺序：

1. 在 [src/common/SettingsManager.hpp](src/common/SettingsManager.hpp) 与 [src/common/SettingsManager.cpp](src/common/SettingsManager.cpp) 增加读写接口。
2. 在 [src/ui/SettingsWindow.cpp](src/ui/SettingsWindow.cpp) 增加表单项与信号。
3. 在 [src/app/main.cpp](src/app/main.cpp) 补齐装配连接。
4. 在 `ChatController` 或 `AgentRuntime` 中消费该设置。

不要：

- 直接在 `ChatController` 中操作配置 JSON。
- 在多个类中重复定义同一设置项的默认值。

#### 8.6 修改聊天持久化格式

如果你修改 `messages` 的结构或筛选逻辑：

1. 优先保持旧数据可读取。
2. 明确哪些字段是“用户可见历史”，哪些是“运行时协议上下文”。
3. 验证旧聊天记录不会导致 provider 报错。
4. 验证清空聊天、切换模型、tool call 后继续对话都正常。

<a id="development-rules"></a>

### 9. 开发规范

#### 9.1 架构约束

- 不要让 UI 层承担 Runtime 状态机职责。
- 不要让 Provider 层感知具体窗口实现。
- 不要让 `SettingsManager` 反向依赖 `src/ai` 具体实现类。
- 新功能优先通过接口抽象挂载，避免把具体供应商逻辑写死在 Runtime 中。

#### 9.2 线程与异步约束

- 阻塞式 MCP 初始化、`listTools()`、`callTool()` 必须放到后台线程执行，避免卡 UI。
- UI 更新必须回到主线程。
- 不要在后台线程直接操作 QWidget。
- 若新增后台任务，请保证失败路径也能回收状态与 busy 标记。

#### 9.3 MCP 相关约束

- 修改 MCP server 配置后，应通过 `markMcpToolsDirty()` 驱动刷新，而不是手工拼接 tools。
- 对未变化的 server 优先复用缓存，不要每次发送消息都全量重建 adapter。
- 新增工具命名规则时，必须提供稳定的反向解析方案。
- 对失败 server 的状态缓存要谨慎，避免不可用状态被错误复用成“永久状态”。

#### 9.4 聊天与协议约束

- 不要把纯协议消息直接展示给用户。
- 修改 `OpenAIChatClient` 的消息正规化逻辑时，要同时验证严格 provider 对 assistant/tool 配对的要求。
- 如需调整 system prompt 行为，注意 `$name$` 变量替换仍需保留或给出兼容迁移方案。

#### 9.5 代码风格与文档约束

- 保持现有 Qt/C++ 风格与命名约定。
- 新增 Agent 相关公共能力时，优先补充本文件和 [CONTRIBUTING.md](CONTRIBUTING.md) 中的说明。
- 如果改动影响英文贡献者理解，请同步更新 [AGENT.en.md](AGENT.en.md) 与 [CONTRIBUTING.en.md](CONTRIBUTING.en.md)。

<a id="debug-and-validation"></a>

### 10. 调试与验证建议

#### 10.1 最低验证清单

任何 Agent 相关改动，至少建议本地验证以下场景：

1. 纯文本聊天成功。
2. 流式输出正常。
3. 关闭 TTS 时文本仍能完整收敛。
4. 开启 TTS 时音频正常生成并播放。
5. MCP server 可加载 tools。
6. 至少一个 tool call 能成功往返。
7. tool call 失败时，错误能回填给模型而不是让 Runtime 卡死。
8. 切换模型后上下文正确隔离。
9. 清空聊天后不会残留 placeholder 或 busy 状态。

#### 10.2 推荐关注日志点

当前可优先关注以下日志域：

- `[AgentRuntime]`
- `[MCP]`
- Provider 返回的错误文本

若新增日志：

- 保持前缀一致，便于过滤。
- 不要打印敏感信息，如 API Key、完整 Authorization header。

#### 10.3 常见故障排查方向

- 模型一直无响应
  - 检查 `aiBaseUrl`、`aiModel`、网络错误、provider 是否拒绝当前 messages 格式。
- tool call 发出后卡住
  - 检查 `tool_call_id` 是否正确保留。
  - 检查 Runtime 是否处于 `m_waitingToolResult`。
  - 检查 `ToolRegistry` 是否能正确回查 server/tool。
- MCP 状态不刷新
  - 检查 `mcpSettingsChanged -> markMcpToolsDirty()` 是否仍连通。
  - 检查缓存是否错误复用了旧状态。
- TTS 成功但 UI 没完成
  - 检查 `requestStartPlayback()`、paced reveal 和 busy 状态释放路径。

<a id="known-boundaries"></a>

### 11. 当前边界与演进方向

当前实现的几个现实边界：

- Skill Registry 仍是占位，尚未形成真正的技能注册与调度框架。
- 当前工具系统核心仍基于 MCP tools，而非仓库内建工具 DSL。
- ChatController 目前承担较多编排责任，未来如果 Agent 能力继续扩展，可能需要拆分出更清晰的 coordinator / runtime service。
- Provider 仍以 OpenAI 兼容协议为中心，若引入其他协议，需要重新审视消息正规化与 tool call 兼容层。

如果你计划做较大 Agent 演进，建议优先保证以下目标：

1. 不破坏现有聊天、TTS、MCP 的工作闭环。
2. 不绕开 `SettingsManager` 与现有持久化路径。
3. 不让 UI 层承担新的协议复杂度。
4. 不在没有兼容策略的情况下更改消息协议格式。
