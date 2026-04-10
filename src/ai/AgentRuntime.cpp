#include "ai/AgentRuntime.hpp"

#include "ai/ConversationRepositoryAdapter.hpp"
#include "ai/OpenAIChatClient.hpp"
#include "ai/core/IChatProvider.hpp"
#include "ai/core/IConversationStore.hpp"
#include "ai/core/ITtsProvider.hpp"
#include "common/SettingsManager.hpp"

#include <QJsonDocument>
#include <QDir>

#include <algorithm>

AgentRuntime::AgentRuntime(QObject* parent)
    : IAgentRuntime(parent)
{
    m_store = &m_defaultStore;
}

void AgentRuntime::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(m_state);
}

void AgentRuntime::setChatProvider(IChatProvider* provider)
{
    if (m_client == provider)
        return;

    if (auto* oldClient = dynamic_cast<QObject*>(m_client))
        disconnect(oldClient, nullptr, this, nullptr);

    m_client = provider;
    if (!m_client)
        return;

    auto* clientObject = dynamic_cast<QObject*>(m_client);
    if (clientObject)
    {
        connect(clientObject, SIGNAL(tokenReceived(QString)), this, SLOT(onClientToken(QString)));
        connect(clientObject, SIGNAL(finished(QString)), this, SLOT(onClientFinished(QString)));
        connect(clientObject, SIGNAL(errorOccurred(QString)), this, SLOT(onClientError(QString)));
        connect(clientObject, SIGNAL(toolCallRequested(QString,QString,QString)), this, SLOT(onClientToolCallRequested(QString,QString,QString)));
    }
}

void AgentRuntime::setConversationStore(IConversationStore* store)
{
    m_store = store ? store : &m_defaultStore;
}

void AgentRuntime::setTtsProvider(ITtsProvider* provider)
{
    if (m_tts == provider)
        return;

    if (auto* oldTts = dynamic_cast<QObject*>(m_tts))
        disconnect(oldTts, nullptr, this, nullptr);

    m_tts = provider;
    if (!m_tts)
        return;

    auto* ttsObject = dynamic_cast<QObject*>(m_tts);
    if (ttsObject)
    {
        connect(ttsObject, SIGNAL(finished(QString)), this, SLOT(onTtsFinished(QString)));
        connect(ttsObject, SIGNAL(errorOccurred(QString)), this, SLOT(onTtsError(QString)));
    }
}

void AgentRuntime::setModelContext(const QString& modelFolder, const QString& modelDir)
{
    m_modelFolder = modelFolder;
    m_modelDir = modelDir;
}

void AgentRuntime::refreshClientConfig()
{
    if (!m_client)
        return;

    IChatProvider::ChatConfig cfg;
    cfg.baseUrl = SettingsManager::instance().aiBaseUrl();
    cfg.apiKey = SettingsManager::instance().aiApiKey();
    cfg.model = SettingsManager::instance().aiModel();
    cfg.systemPrompt = SettingsManager::instance().aiSystemPrompt();
    cfg.stream = SettingsManager::instance().aiStreamEnabled();

    if (!m_modelFolder.isEmpty())
        cfg.systemPrompt.replace(QStringLiteral("$name$"), m_modelFolder);

    m_client->setConfig(cfg);
}

void AgentRuntime::refreshTtsConfig()
{
    if (!m_tts)
    {
        m_ttsEnabled = false;
        return;
    }

    ITtsProvider::TtsConfig cfg;
    cfg.baseUrl = SettingsManager::instance().ttsBaseUrl();
    cfg.apiKey = SettingsManager::instance().ttsApiKey();
    cfg.model = SettingsManager::instance().ttsModel();
    cfg.voice = SettingsManager::instance().ttsVoice();

    m_tts->setConfig(cfg);
    m_ttsEnabled = !cfg.baseUrl.trimmed().isEmpty();
}

QString AgentRuntime::cacheAudioPathForModel() const
{
    const QString cacheDir = SettingsManager::instance().cacheDir();
    QDir dir(cacheDir);
    if (!dir.exists())
        dir.mkpath(cacheDir);

    return cacheDir + QStringLiteral("/") + m_modelFolder + QStringLiteral(".wav");
}

void AgentRuntime::appendUserMessage(const QString& userText)
{
    if (!m_store || m_modelFolder.isEmpty())
        return;

    QJsonObject o;
    o["role"] = QStringLiteral("user");
    o["content"] = userText;
    m_messages.append(o);
    m_store->saveMessages(m_modelFolder, visibleMessagesForPersistence());
}

void AgentRuntime::appendAiPlaceholder()
{
    if (!m_store || m_modelFolder.isEmpty())
        return;

    QJsonObject o;
    o["role"] = QStringLiteral("assistant");
    o["content"] = QString();
    m_messages.append(o);
    m_hasAssistantPlaceholder = true;
    m_store->saveMessages(m_modelFolder, visibleMessagesForPersistence());
}

void AgentRuntime::finalizeAssistantMessage(const QString& fullText)
{
    if (!m_store || m_modelFolder.isEmpty())
        return;

    if (m_hasAssistantPlaceholder && !m_messages.isEmpty())
    {
        auto last = m_messages.last().toObject();
        if (last.value(QStringLiteral("role")).toString() == QStringLiteral("assistant"))
        {
            last[QStringLiteral("content")] = fullText;
            m_messages[m_messages.size() - 1] = last;
            m_store->saveMessages(m_modelFolder, visibleMessagesForPersistence());
            return;
        }
    }

    QJsonObject o;
    o["role"] = QStringLiteral("assistant");
    o["content"] = fullText;
    m_messages.append(o);
    m_store->saveMessages(m_modelFolder, visibleMessagesForPersistence());
}

QJsonArray AgentRuntime::visibleMessagesForPersistence() const
{
    QJsonArray out;

    for (const QJsonValue& v : m_messages)
    {
        if (!v.isObject())
            continue;

        const QJsonObject obj = v.toObject();
        const QString role = obj.value(QStringLiteral("role")).toString();

        // Protocol-only tool payloads should not be persisted into user chat history.
        if (role == QStringLiteral("tool"))
            continue;

        if (role == QStringLiteral("assistant"))
        {
            const QString content = obj.value(QStringLiteral("content")).toString();

            // Drop placeholders and assistant tool_call protocol envelopes.
            if (content.trimmed().isEmpty())
                continue;

            QJsonObject visible;
            visible.insert(QStringLiteral("role"), QStringLiteral("assistant"));
            visible.insert(QStringLiteral("content"), content);
            out.append(visible);
            continue;
        }

        if (role == QStringLiteral("user"))
        {
            QJsonObject visible;
            visible.insert(QStringLiteral("role"), QStringLiteral("user"));
            visible.insert(QStringLiteral("content"), obj.value(QStringLiteral("content")).toString());
            out.append(visible);
        }
    }

    return out;
}

void AgentRuntime::submitUserMessage(const QString& userText)
{
    if (m_busy || !m_client)
        return;

    if (m_modelFolder.isEmpty() || m_modelDir.isEmpty())
    {
        emit errorOccurred(QStringLiteral("Model context not set. Call setModelContext() first."));
        setState(State::Failed);
        return;
    }

    if (userText.trimmed().isEmpty())
        return;

    m_messages = m_store->loadMessages(m_modelFolder);
    appendUserMessage(userText);
    appendAiPlaceholder();

    m_pendingStreamText.clear();
    m_pendingStreamText.reserve(std::max<qsizetype>(256, userText.size() * 4));
    m_pendingFinalText.clear();
    m_waitingToolResult = false;
    m_pendingToolCallId.clear();
    m_pendingToolName.clear();
    m_pendingToolInput.clear();
    m_pendingToolResult.clear();
    m_hasPendingToolResult = false;

    emit requestAppendUserMessage(userText);
    emit requestAppendAiMessageStart();
    emit requestSetBusy(true);

    setState(State::Submitting);
    refreshClientConfig();

    const QByteArray msgJson = QJsonDocument(m_messages).toJson(QJsonDocument::Compact);
    m_busy = true;
    m_client->startChat(msgJson);
}

void AgentRuntime::clearConversation(const QString& modelFolder)
{
    m_store->saveMessages(modelFolder, QJsonArray{});
    if (modelFolder == m_modelFolder)
    {
        m_messages = QJsonArray{};
        m_pendingFinalText.clear();
        m_pendingStreamText.clear();
        m_pendingToolCallId.clear();
        m_pendingToolName.clear();
        m_pendingToolInput.clear();
        m_pendingToolResult.clear();
        m_hasPendingToolResult = false;
        m_hasAssistantPlaceholder = false;
        m_busy = false;
    }

    emit requestConversationCleared(modelFolder);
    setState(State::Idle);
}

void AgentRuntime::cancel()
{
    if (m_client)
        m_client->cancel();
    m_busy = false;
    m_pendingFinalText.clear();
    m_pendingStreamText.clear();
    m_pendingToolCallId.clear();
    m_pendingToolName.clear();
    m_pendingToolInput.clear();
    m_pendingToolResult.clear();
    m_hasPendingToolResult = false;
    m_hasAssistantPlaceholder = false;
    emit requestSetBusy(false);
    setState(State::Idle);
}

void AgentRuntime::submitToolResult(const QString& toolName, const QString& toolInput, const QString& result)
{
    if (!m_waitingToolResult || !m_client || !m_store || m_modelFolder.isEmpty())
        return;

    m_pendingToolName = toolName;
    m_pendingToolInput = toolInput;
    m_pendingToolResult = result;
    m_hasPendingToolResult = true;

    if (m_client->isBusy())
        return;

    QJsonObject toolMsg;
    toolMsg[QStringLiteral("role")] = QStringLiteral("tool");
    toolMsg[QStringLiteral("content")] = m_pendingToolResult;
    if (m_pendingToolCallId.isEmpty())
        return;
    toolMsg[QStringLiteral("tool_call_id")] = m_pendingToolCallId;
    m_messages.append(toolMsg);
    m_store->saveMessages(m_modelFolder, visibleMessagesForPersistence());

    // Continue the conversation with tool output in context.
    appendAiPlaceholder();
    const QByteArray msgJson = QJsonDocument(m_messages).toJson(QJsonDocument::Compact);
    m_waitingToolResult = false;
    m_pendingToolCallId.clear();
    m_pendingToolName.clear();
    m_pendingToolInput.clear();
    m_pendingToolResult.clear();
    m_hasPendingToolResult = false;
    m_busy = true;
    setState(State::Submitting);
    refreshClientConfig();
    m_client->startChat(msgJson);
}

void AgentRuntime::onClientToken(const QString& token)
{
    setState(State::Streaming);
    m_pendingStreamText += token;  // Accumulate for paced reveal
    emit tokenReceived(token);
}

void AgentRuntime::onClientFinished(const QString& fullText)
{
    if (m_waitingToolResult && fullText.trimmed().isEmpty() && m_hasPendingToolResult)
    {
        submitToolResult(m_pendingToolName, m_pendingToolInput, m_pendingToolResult);
        return;
    }

    // Tool-call phase: keep runtime busy, finalize pre-tool assistant text as its own
    // message when provided, then open a new assistant draft bubble for tool
    // execution / follow-up response.
    if (m_waitingToolResult)
    {
        const QString preToolText = fullText.trimmed();
        if (!preToolText.isEmpty())
        {
            refreshTtsConfig();
            m_pendingFinalText = preToolText;

            if (m_ttsEnabled && m_tts)
            {
                const QString audioPath = cacheAudioPathForModel();
                const bool streamingEnabled = SettingsManager::instance().aiStreamEnabled();
                emit requestStartTts(preToolText, audioPath, streamingEnabled);
                return;
            }

            finalizeAssistantMessage(preToolText);
            m_pendingFinalText.clear();
            m_hasAssistantPlaceholder = false;
            emit finished(preToolText);
        }

        // Ensure UI remains blocked and shows waiting dots while tool is running.
        emit requestSetBusy(true);
        emit requestAppendAiMessageStart();
        return;
    }

    refreshTtsConfig();
    m_pendingFinalText = fullText;

    if (m_ttsEnabled && m_tts)
    {
        // Defer finalization until TTS is ready to play.
        const QString audioPath = cacheAudioPathForModel();
        const bool streamingEnabled = SettingsManager::instance().aiStreamEnabled();
        emit requestStartTts(fullText, audioPath, streamingEnabled);
        // Keep busy until audio playback ends.
        return;
    }

    // No TTS => finalize immediately.
    finalizeAssistantMessage(fullText);
    m_busy = false;
    emit finished(fullText);
    m_hasAssistantPlaceholder = false;
    setState(State::Completed);
}

void AgentRuntime::onTtsFinished(const QString& outPath)
{
    const QString finalizedText = m_pendingFinalText;

    if (!outPath.isEmpty())
    {
        emit requestStartPlayback(outPath);
    }

    // If streaming is enabled and we have streamed tokens, reveal them paced to audio.
    const bool wantStream = SettingsManager::instance().aiStreamEnabled();
    if (wantStream && !m_pendingStreamText.isEmpty())
    {
        emit requestStartPacedTextReveal(m_pendingStreamText);
        m_pendingStreamText.clear();

        // Persist finalized text before paced reveal starts.
        if (!finalizedText.isEmpty())
        {
            finalizeAssistantMessage(finalizedText);
            m_pendingFinalText.clear();
        }

        if (m_waitingToolResult)
        {
            // This segment is done; keep waiting for tool result / follow-up turn.
            emit finished(finalizedText);
            m_hasAssistantPlaceholder = false;
            emit requestSetBusy(true);
            emit requestAppendAiMessageStart();
            setState(State::Submitting);
            return;
        }

        m_busy = false;
        emit finished(finalizedText);
        m_hasAssistantPlaceholder = false;
        setState(State::Completed);
        return;
    }

    // Non-stream (or no tokens captured): finalize immediately.
    if (!finalizedText.isEmpty())
    {
        finalizeAssistantMessage(finalizedText);
        m_pendingFinalText.clear();
    }

    if (m_waitingToolResult)
    {
        emit finished(finalizedText);
        m_hasAssistantPlaceholder = false;
        emit requestSetBusy(true);
        emit requestAppendAiMessageStart();
        setState(State::Submitting);
        return;
    }

    m_busy = false;
    emit finished(finalizedText);
    m_hasAssistantPlaceholder = false;
    setState(State::Completed);
}

void AgentRuntime::onTtsError(const QString& message)
{
    const QString finalizedText = m_pendingFinalText;
    emit ttsError(message);
    // Fall back: finalize message anyway and reset busy
    if (!finalizedText.isEmpty())
    {
        finalizeAssistantMessage(finalizedText);
        m_pendingFinalText.clear();
    }

    if (m_waitingToolResult)
    {
        emit finished(finalizedText);
        m_hasAssistantPlaceholder = false;
        emit requestSetBusy(true);
        emit requestAppendAiMessageStart();
        setState(State::Submitting);
        return;
    }

    m_busy = false;
    emit finished(finalizedText);
    m_hasAssistantPlaceholder = false;
    setState(State::Completed);
}

void AgentRuntime::onClientToolCallRequested(const QString& toolName, const QString& toolInput, const QString& toolCallId)
{
    if (toolName.trimmed().isEmpty())
        return;

    if (toolCallId.trimmed().isEmpty())
    {
        emit errorOccurred(QStringLiteral("Invalid tool call: missing tool_call_id"));
        return;
    }

    if (m_waitingToolResult)
    {
        emit errorOccurred(QStringLiteral("Ignoring extra tool call while waiting for previous tool result"));
        return;
    }

    // Remove empty assistant placeholder before tool phase.
    if (m_hasAssistantPlaceholder && !m_messages.isEmpty())
    {
        const QJsonObject last = m_messages.last().toObject();
        if (last.value(QStringLiteral("role")).toString() == QStringLiteral("assistant") &&
            last.value(QStringLiteral("content")).toString().isEmpty())
        {
            m_messages.removeLast();
            m_store->saveMessages(m_modelFolder, visibleMessagesForPersistence());
            m_hasAssistantPlaceholder = false;
        }
    }

    // Persist assistant tool_calls message before tool output so providers can validate history.
    {
        QJsonObject fnObj;
        fnObj[QStringLiteral("name")] = toolName;
        fnObj[QStringLiteral("arguments")] = toolInput;

        QJsonObject toolCallObj;
        toolCallObj[QStringLiteral("id")] = toolCallId;
        toolCallObj[QStringLiteral("type")] = QStringLiteral("function");
        toolCallObj[QStringLiteral("function")] = fnObj;

        QJsonObject assistantMsg;
        assistantMsg[QStringLiteral("role")] = QStringLiteral("assistant");
        assistantMsg[QStringLiteral("content")] = QString();
        assistantMsg[QStringLiteral("tool_calls")] = QJsonArray{toolCallObj};
        m_messages.append(assistantMsg);
        m_store->saveMessages(m_modelFolder, visibleMessagesForPersistence());
    }

    m_waitingToolResult = true;
    m_pendingToolCallId = toolCallId;
    emit toolCallRequested(toolName, toolInput);
}

void AgentRuntime::onClientError(const QString& message)
{
    m_busy = false;
    emit errorOccurred(message);
    setState(State::Failed);
}