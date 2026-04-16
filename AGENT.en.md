# Agent Development Guide

<p><a href="AGENT.md#zh-cn">简体中文</a> | <a href="AGENT.en.md#en-us">English</a></p>

<a id="en-us"></a>

## English

This document is for contributors who want to develop, refactor, debug, or extend the current Agent capabilities in AmaiGirl. It covers the current framework, technology stack, module responsibilities, key paths, configuration and persistence, extension points, engineering rules, and validation guidance.

Please read this together with:

- [Contributing Guide](CONTRIBUTING.en.md)
- [Chinese Agent Guide](AGENT.md)

### Contents

- [1. What the current Agent is](#agent-positioning)
- [2. Framework overview](#agent-architecture)
- [3. Technology stack](#tech-stack)
- [4. Runtime flow](#runtime-flow)
- [5. Module layout and responsibilities](#module-layout)
- [6. Key paths and task index](#key-paths)
- [7. Configuration, state, and persistence](#config-and-persistence)
- [8. Development playbook](#development-playbook)
- [9. Engineering rules](#development-rules)
- [10. Debugging and validation](#debug-and-validation)
- [11. Current boundaries and future evolution](#known-boundaries)

<a id="agent-positioning"></a>

### 1. What the current Agent is

The current implementation is not a standalone multi-process Agent platform. It is an embedded Agent Runtime system inside the Qt desktop application. Its main goals are:

- Integrate chat, tool invocation, TTS, playback, and UI presentation into one runtime path.
- Use OpenAI-compatible APIs for LLM and TTS requests.
- Expose external tools to the model through MCP (Model Context Protocol).
- Keep chat state, tool state, and UI state consistent without blocking the desktop pet rendering loop.

The current backbone is:

- `ChatController` for orchestration.
- `AgentRuntime` for conversation state and persistence.
- `OpenAIChatClient` and `OpenAITtsClient` for provider access.
- `McpStdioAdapter` and `McpHttpSseAdapter` for tool access.
- `ChatWindow` and `SettingsWindow` for interaction and configuration.

Important notes:

- Runtime is now the only active path. `SettingsManager::useAgentRuntime()` is kept only for compatibility and always returns true.
- `ISkillRegistry` is currently only a placeholder. There is no full standalone Skill subsystem yet. If you plan to extend the Agent, the primary extension axes are Runtime, Providers, MCP, and UI wiring.

<a id="agent-architecture"></a>

### 2. Framework overview

#### 2.1 Layering

The current Agent can be understood in the following layers:

1. Application composition layer
   - [src/app/main.cpp](src/app/main.cpp)
   - Creates `SettingsWindow`, `ChatWindow`, `ChatController`, and `Renderer`, and wires signals.
2. Orchestration layer
   - [src/ai/ChatController.hpp](src/ai/ChatController.hpp)
   - [src/ai/ChatController.cpp](src/ai/ChatController.cpp)
   - Handles model switching, sending messages, MCP tool reloads, tool routing, TTS/playback, and UI busy state.
3. Runtime layer
   - [src/ai/AgentRuntime.hpp](src/ai/AgentRuntime.hpp)
   - [src/ai/AgentRuntime.cpp](src/ai/AgentRuntime.cpp)
   - Owns the conversation state machine, message buffers, assistant placeholders, tool result continuation, and persistence filtering.
4. Provider layer
   - Chat: [src/ai/OpenAIChatClient.hpp](src/ai/OpenAIChatClient.hpp), [src/ai/OpenAIChatClient.cpp](src/ai/OpenAIChatClient.cpp)
   - TTS: [src/ai/OpenAITtsClient.hpp](src/ai/OpenAITtsClient.hpp), [src/ai/OpenAITtsClient.cpp](src/ai/OpenAITtsClient.cpp)
   - Talks to OpenAI-compatible endpoints.
5. Tool / MCP integration layer
   - [src/ai/core/IMcpAdapter.hpp](src/ai/core/IMcpAdapter.hpp)
   - [src/ai/McpStdioAdapter.hpp](src/ai/McpStdioAdapter.hpp)
   - [src/ai/McpHttpSseAdapter.hpp](src/ai/McpHttpSseAdapter.hpp)
   - [src/ai/core/ToolRegistry.hpp](src/ai/core/ToolRegistry.hpp)
   - Discovers tools, manages naming and routing, and executes tool calls.
6. Configuration and persistence layer
   - [src/common/SettingsManager.hpp](src/common/SettingsManager.hpp)
   - [src/ai/ConversationRepository.hpp](src/ai/ConversationRepository.hpp)
   - [src/ai/ConversationRepositoryAdapter.hpp](src/ai/ConversationRepositoryAdapter.hpp)
   - Owns config.json, mcp.json, chat history, and cache paths.
7. UI presentation layer
   - [src/ui/ChatWindow.hpp](src/ui/ChatWindow.hpp)
   - [src/ui/SettingsWindow.hpp](src/ui/SettingsWindow.hpp)
   - [src/ui/McpServerEditorDialog.hpp](src/ui/McpServerEditorDialog.hpp)
   - Handles input, state display, MCP toggles, and configuration editing.

#### 2.2 Design rules

- `ChatController` is the orchestration entry point. Do not scatter multi-module flow logic across UI classes.
- `AgentRuntime` is the single source of truth for conversation state and chat persistence. When Runtime is active, `ChatWindow` is render-only and must not write chat JSON directly.
- Providers and MCP adapters are isolated behind interfaces so they can be replaced later.
- Tool names exposed to the model are decoupled from raw server tool names via `ToolRegistry`.
- Settings must flow through `SettingsManager` first, then trigger refresh through signals. Do not write raw JSON directly in business logic.

<a id="tech-stack"></a>

### 3. Technology stack

- Language standard: C++20
- UI and infrastructure: Qt 6
  - Core
  - Gui
  - Widgets
  - Network
  - Multimedia
  - OpenGL / OpenGLWidgets
- Build system: CMake + Ninja
- Model API: OpenAI-compatible Chat Completions
- TTS API: OpenAI-compatible Audio Speech
- Tool protocol: MCP
  - Stdio
  - HTTP/SSE
- Persistence format: JSON
- Concurrency model: Qt event loop + signals/slots + `QThread::create()` background jobs

The most relevant Qt components for the Agent subsystem are:

- `QNetworkAccessManager` / `QNetworkReply`
- `QJsonObject` / `QJsonArray` / `QJsonDocument`
- `QThread`
- `QMediaPlayer` / `QAudioOutput`
- `QObject` signal/slot system

<a id="runtime-flow"></a>

### 4. Runtime flow

#### 4.1 Startup and composition

Startup composition lives in [src/app/main.cpp](src/app/main.cpp):

1. Create `SettingsWindow`.
2. Create `ChatWindow`.
3. Create `ChatController`.
4. Pass `ChatWindow` and `Renderer` to `ChatController`.
5. Connect `SettingsWindow::mcpSettingsChanged` to `ChatController::markMcpToolsDirty()`.
6. On model changes, update model context through `ChatController::onModelChanged()`.

#### 4.2 Message send flow

The main send path is:

1. `ChatWindow` emits the send signal.
2. `ChatController` prepares current model context, MCP tools, and UI busy state.
3. `AgentRuntime::submitUserMessage()` loads history, appends the user message, and inserts an assistant placeholder.
4. `AgentRuntime` refreshes chat config from `SettingsManager` and calls `IChatProvider::startChat()`.
5. `OpenAIChatClient` sends the request and streams tokens back through signals.
6. `AgentRuntime` maintains the state machine and decides whether to finish immediately, go through TTS first, or enter the tool-call phase.

#### 4.3 Tool-call flow

When the model requests a tool call, the current round-trip is:

1. `OpenAIChatClient` parses `tool_calls` from the provider response.
2. It emits `toolCallRequested(toolName, toolInput, toolCallId)`.
3. `AgentRuntime::onClientToolCallRequested()`:
   - Removes the empty assistant placeholder.
   - Persists the assistant tool-calls envelope into in-memory conversation context.
   - Sets `m_waitingToolResult = true`.
   - Emits `toolCallRequested(toolName, toolInput)`.
4. `ChatController::onRuntimeToolCallRequested()`:
   - Resolves the exposed tool name to `serverName + rawToolName` through `ToolRegistry`.
   - Falls back to heuristic parsing if the registry misses.
   - Creates the correct `IMcpAdapter` in a background thread and executes `callTool()`.
5. `ChatController` sends the result back through `AgentRuntime::submitToolResult()`.
6. `AgentRuntime` appends a tool message and continues the conversation with the chat provider.

Important constraints:

- Tool result messages are protocol-only context and are not directly persisted into user-visible chat history.
- Runtime filters empty assistant messages and protocol-only tool payloads to keep chat history clean.

#### 4.4 MCP tool reload flow

MCP tool refresh is concentrated in `ChatController::reloadMcpTools()`:

1. Read `SettingsManager::mcpServers()`.
2. Compare config cache, raw tool cache, and status cache to decide which servers need reload.
3. Reuse cached results for unchanged servers.
4. For changed servers, create adapters in a background thread and call `listTools()`.
5. Merge tools from all servers into OpenAI function tools.
6. Use `ToolRegistry` to map exposed names to raw routes.
7. Call `OpenAIChatClient::setTools()`.
8. Publish server statuses to the ChatWindow MCP popup.

#### 4.5 TTS and playback flow

1. After text generation, `AgentRuntime` checks whether TTS is enabled based on `ttsBaseUrl`.
2. If enabled, it emits `requestStartTts()` to `ChatController`.
3. `ChatController` calls `OpenAITtsClient::startSpeech()` and writes audio to disk.
4. On success it:
   - Starts playback.
   - Starts paced text reveal synced to playback.
   - Persists the final assistant text.

<a id="module-layout"></a>

### 5. Module layout and responsibilities

#### 5.1 Main Agent-related directories

- [src/ai](src/ai)
  - Runtime implementation, providers, MCP adapters, and conversation persistence adapters.
- [src/ai/core](src/ai/core)
  - Core interfaces, MCP config structures, ToolRegistry, and status types.
- [src/common](src/common)
  - Global settings and path management.
- [src/ui](src/ui)
  - Chat window, settings window, MCP configuration dialogs, and related UI widgets.
- [src/app](src/app)
  - Application composition and signal wiring.

#### 5.2 File responsibility index for src/ai

- [src/ai/ChatController.cpp](src/ai/ChatController.cpp)
  - Main Agent orchestration.
  - MCP tool discovery, caching, and status publishing.
  - Actual tool execution.
  - TTS and playback coordination.
- [src/ai/AgentRuntime.cpp](src/ai/AgentRuntime.cpp)
  - Runtime state machine.
  - Message buffering and persistence filtering.
  - Continue-after-tool-result logic.
- [src/ai/OpenAIChatClient.cpp](src/ai/OpenAIChatClient.cpp)
  - OpenAI-compatible chat requests.
  - SSE token parsing.
  - Tool-call extraction and forwarding.
- [src/ai/OpenAITtsClient.cpp](src/ai/OpenAITtsClient.cpp)
  - OpenAI-compatible TTS requests.
  - Audio output writing and transcoding compatibility.
- [src/ai/McpStdioAdapter.cpp](src/ai/McpStdioAdapter.cpp)
  - MCP communication through process stdio.
- [src/ai/McpHttpSseAdapter.cpp](src/ai/McpHttpSseAdapter.cpp)
  - MCP communication through HTTP/SSE.
- [src/ai/ConversationRepository.cpp](src/ai/ConversationRepository.cpp)
  - Per-model chat history load/save.

#### 5.3 File responsibility index for src/ai/core

- [src/ai/core/IAgentRuntime.hpp](src/ai/core/IAgentRuntime.hpp)
  - Runtime-facing abstraction and state signals.
- [src/ai/core/IChatProvider.hpp](src/ai/core/IChatProvider.hpp)
  - Chat provider interface.
- [src/ai/core/ITtsProvider.hpp](src/ai/core/ITtsProvider.hpp)
  - TTS provider interface.
- [src/ai/core/IMcpAdapter.hpp](src/ai/core/IMcpAdapter.hpp)
  - MCP adapter abstraction and factory entry.
- [src/ai/core/IMcpHttpSseAdapter.hpp](src/ai/core/IMcpHttpSseAdapter.hpp)
  - HTTP/SSE-specific adapter interface.
- [src/ai/core/ToolRegistry.hpp](src/ai/core/ToolRegistry.hpp)
  - Mapping from exposed tool name to actual route.
- [src/ai/core/McpServerConfig.hpp](src/ai/core/McpServerConfig.hpp)
  - MCP server configuration, serialization, and validation.
- [src/ai/core/McpServerStatus.hpp](src/ai/core/McpServerStatus.hpp)
  - MCP server UI status model.
- [src/ai/core/ISkillRegistry.hpp](src/ai/core/ISkillRegistry.hpp)
  - Placeholder only at the moment.

<a id="key-paths"></a>

### 6. Key paths and task index

This index is organized by task: if you want to change a feature, start from these files.

| Task | Start here |
| --- | --- |
| Change main Agent flow | [src/ai/ChatController.cpp](src/ai/ChatController.cpp), [src/ai/AgentRuntime.cpp](src/ai/AgentRuntime.cpp) |
| Add or replace a chat provider | [src/ai/core/IChatProvider.hpp](src/ai/core/IChatProvider.hpp), [src/ai/OpenAIChatClient.cpp](src/ai/OpenAIChatClient.cpp), [src/ai/ChatController.cpp](src/ai/ChatController.cpp) |
| Add or replace a TTS provider | [src/ai/core/ITtsProvider.hpp](src/ai/core/ITtsProvider.hpp), [src/ai/OpenAITtsClient.cpp](src/ai/OpenAITtsClient.cpp), [src/ai/ChatController.cpp](src/ai/ChatController.cpp) |
| Add a new MCP transport type | [src/ai/core/IMcpAdapter.hpp](src/ai/core/IMcpAdapter.hpp), [src/ai/core/McpServerConfig.hpp](src/ai/core/McpServerConfig.hpp), [src/ui/McpServerEditorDialog.cpp](src/ui/McpServerEditorDialog.cpp) |
| Change MCP tool reload or caching | [src/ai/ChatController.cpp](src/ai/ChatController.cpp), [src/ai/core/ToolRegistry.cpp](src/ai/core/ToolRegistry.cpp) |
| Change tool exposure naming | [src/ai/ChatController.cpp](src/ai/ChatController.cpp), [src/ai/core/ToolRegistry.hpp](src/ai/core/ToolRegistry.hpp) |
| Change chat persistence | [src/ai/AgentRuntime.cpp](src/ai/AgentRuntime.cpp), [src/ai/ConversationRepository.cpp](src/ai/ConversationRepository.cpp) |
| Add or change Agent settings | [src/common/SettingsManager.hpp](src/common/SettingsManager.hpp), [src/common/SettingsManager.cpp](src/common/SettingsManager.cpp), [src/ui/SettingsWindow.cpp](src/ui/SettingsWindow.cpp) |
| Change MCP settings UI | [src/ui/SettingsWindow.cpp](src/ui/SettingsWindow.cpp), [src/ui/McpServerCard.cpp](src/ui/McpServerCard.cpp), [src/ui/McpServerEditorDialog.cpp](src/ui/McpServerEditorDialog.cpp) |
| Change MCP status popup in chat window | [src/ui/ChatWindow.cpp](src/ui/ChatWindow.cpp), [src/ai/core/McpServerStatus.hpp](src/ai/core/McpServerStatus.hpp), [src/ai/ChatController.cpp](src/ai/ChatController.cpp) |
| Change top-level wiring | [src/app/main.cpp](src/app/main.cpp) |

<a id="config-and-persistence"></a>

### 7. Configuration, state, and persistence

#### 7.1 Configuration source

All Agent-related settings are managed through `SettingsManager`:

- Chat settings
  - `aiBaseUrl`
  - `aiApiKey`
  - `aiModel`
  - `aiSystemPrompt`
  - `aiStreamEnabled`
- TTS settings
  - `ttsBaseUrl`
  - `ttsApiKey`
  - `ttsModel`
  - `ttsVoice`
- MCP settings
  - `mcpServers()`
  - `setMcpServers()`
  - `addMcpServer()`
  - `updateMcpServer()`
  - `removeMcpServer()`

#### 7.2 JSON file split

- `config.json`
  - Global AI, TTS, UI, and related settings.
- `mcp.json`
  - MCP server definitions.
- `*.chat.json`
  - Per-model persisted chat history.

Rules:

- Do not add raw JSON reads/writes that bypass `SettingsManager`.
- If you rename fields or migrate structure, you must consider backward compatibility and migration.

#### 7.3 Visibility rules for chat history

`AgentRuntime::visibleMessagesForPersistence()` currently filters out:

- Protocol messages with `role == tool`.
- Empty assistant placeholders.
- Assistant tool-call envelopes that exist only for protocol correctness.

This means:

- User-visible chat history stays clean.
- Protocol messages still exist in in-memory runtime context.
- If you change this rule, you must validate chat rendering, conversation continuation, and tool-result round-trips together.

<a id="development-playbook"></a>

### 8. Development playbook

#### 8.1 Add a new chat provider

Recommended steps:

1. Check whether [src/ai/core/IChatProvider.hpp](src/ai/core/IChatProvider.hpp) is sufficient.
2. Add a provider implementation modeled after `OpenAIChatClient`.
3. Be explicit about support for:
   - token streaming
   - tool calls
   - cancelation
4. Wire the provider in [src/ai/ChatController.cpp](src/ai/ChatController.cpp).
5. Validate that `AgentRuntime` still handles tool calls and errors correctly with the new provider.

Do not:

- Make `AgentRuntime` depend on a concrete provider type.
- Send network requests directly from UI classes.

#### 8.2 Add a new TTS provider

Recommended steps:

1. Follow [src/ai/core/ITtsProvider.hpp](src/ai/core/ITtsProvider.hpp).
2. Keep output file path ownership on the caller side.
3. Return understandable error messages.
4. Validate that playback and lip-sync logic in `ChatController` does not rely on a private audio format assumption.

#### 8.3 Add a new MCP adapter type

The repository currently supports:

- Stdio
- HTTP/SSE

If you add another transport type, you must update at least:

1. [src/ai/core/McpServerConfig.hpp](src/ai/core/McpServerConfig.hpp)
   - Add the `Type` enum value and serialization.
2. [src/ai/core/IMcpAdapter.cpp](src/ai/core/IMcpAdapter.cpp)
   - Add factory dispatch.
3. The new adapter implementation.
4. [src/ui/McpServerEditorDialog.cpp](src/ui/McpServerEditorDialog.cpp)
   - Add editing UI for the new config type.
5. Validation and test-connection logic when needed.

#### 8.4 Change tool exposure or routing rules

The current exposed tool name is generated inside `ChatController`, typically in the form:

- `mcp_<server>__<tool>`

If you change it, you must consider:

- legal naming
- truncation behavior
- collision handling
- reverse resolution stability
- compatibility with historical tool-call messages

If you only want a different display label, prefer solving it in UI instead of changing the provider-visible protocol name.

#### 8.5 Add a new Agent setting

Recommended order:

1. Add read/write APIs in [src/common/SettingsManager.hpp](src/common/SettingsManager.hpp) and [src/common/SettingsManager.cpp](src/common/SettingsManager.cpp).
2. Add the form field and signals in [src/ui/SettingsWindow.cpp](src/ui/SettingsWindow.cpp).
3. Wire it in [src/app/main.cpp](src/app/main.cpp).
4. Consume it in `ChatController` or `AgentRuntime`.

Do not:

- Manipulate config JSON directly inside `ChatController`.
- Duplicate the same setting default in multiple classes.

#### 8.6 Change chat persistence format

If you change the `messages` structure or filtering rules:

1. Prefer preserving readability of existing data.
2. Be explicit about which fields are user-visible history and which are runtime-only protocol context.
3. Validate that old chat history does not break provider requests.
4. Validate clear-chat, model switching, and continue-after-tool-call behavior.

<a id="development-rules"></a>

### 9. Engineering rules

#### 9.1 Architecture rules

- Do not move runtime-state-machine responsibility into the UI layer.
- Do not let provider code know about concrete window classes.
- Do not make `SettingsManager` depend on concrete implementation classes from `src/ai`.
- Prefer extending through interfaces instead of hardcoding vendor-specific logic into Runtime.

#### 9.2 Threading and async rules

- Blocking MCP initialization, `listTools()`, and `callTool()` must stay off the UI thread.
- UI updates must return to the main thread.
- Never touch QWidget directly from background threads.
- If you add background work, make sure failure paths also release busy state and runtime state correctly.

#### 9.3 MCP-specific rules

- After MCP config changes, refresh through `markMcpToolsDirty()` rather than manually assembling tools.
- Reuse caches for unchanged servers instead of rebuilding adapters on every send.
- If you change tool naming, provide a stable reverse resolution path.
- Be careful with failed-server status caching so an unavailable state does not become a stale permanent state.

#### 9.4 Chat and protocol rules

- Do not display protocol-only messages directly to the user.
- If you change message normalization in `OpenAIChatClient`, validate strict-provider requirements for assistant/tool pairing.
- If you change system prompt behavior, preserve `$name$` substitution or provide a clear compatibility plan.

#### 9.5 Style and documentation rules

- Follow the existing Qt/C++ style and naming.
- If you add public Agent-related capability, update this file and [CONTRIBUTING.en.md](CONTRIBUTING.en.md) when needed.
- If the change affects Chinese contributors too, update [AGENT.md](AGENT.md) and [CONTRIBUTING.md](CONTRIBUTING.md) in the same PR.

<a id="debug-and-validation"></a>

### 10. Debugging and validation

#### 10.1 Minimum validation checklist

For any Agent-related change, local validation should cover at least:

1. Plain text chat succeeds.
2. Streaming output works.
3. With TTS disabled, text still finishes correctly.
4. With TTS enabled, audio is generated and played.
5. MCP servers can load tools.
6. At least one tool call completes end to end.
7. When a tool call fails, the error is returned to the model and Runtime does not get stuck.
8. Model switching keeps contexts isolated.
9. Clear-chat leaves no placeholder or stuck busy state.

#### 10.2 Recommended log focus

Start with these log prefixes:

- `[AgentRuntime]`
- `[MCP]`
- provider error messages

If you add logs:

- Keep prefixes consistent.
- Do not print secrets such as API keys or full Authorization headers.

#### 10.3 Common failure directions

- Model never responds
  - Check `aiBaseUrl`, `aiModel`, network errors, and whether the provider rejects the current message format.
- Tool call is requested and then stalls
  - Check whether `tool_call_id` is preserved.
  - Check whether Runtime is still in `m_waitingToolResult`.
  - Check whether `ToolRegistry` can resolve the server and tool.
- MCP status does not refresh
  - Check whether `mcpSettingsChanged -> markMcpToolsDirty()` is still wired.
  - Check whether stale cache is being reused incorrectly.
- TTS succeeds but UI does not finish
  - Check `requestStartPlayback()`, paced reveal, and busy-state release paths.

<a id="known-boundaries"></a>

### 11. Current boundaries and future evolution

Some practical boundaries of the current implementation are:

- Skill Registry is still only a placeholder.
- The current tool system is centered on MCP tools, not an internal tool DSL.
- `ChatController` currently owns a lot of orchestration responsibility. If Agent capabilities grow further, it may need to split into clearer coordinator/runtime services.
- Providers are still centered on OpenAI-compatible protocols. If new protocols are introduced, message normalization and tool-call compatibility will need to be revisited.

If you plan a larger Agent evolution, prioritize these goals:

1. Do not break the existing chat, TTS, and MCP working loop.
2. Do not bypass `SettingsManager` or existing persistence paths.
3. Do not move new protocol complexity into UI code.
4. Do not change message protocol format without a compatibility strategy.
