#pragma once

#include "ai/core/IAgentRuntime.hpp"
#include "ai/ConversationRepositoryAdapter.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QString>

class IChatProvider;
class IConversationStore;
class ITtsProvider;

class AgentRuntime final : public IAgentRuntime
{
    Q_OBJECT
public:
    explicit AgentRuntime(QObject* parent = nullptr);

    void setChatProvider(IChatProvider* provider);
    void setConversationStore(IConversationStore* store);
    void setTtsProvider(ITtsProvider* provider) override;

    void setModelContext(const QString& modelFolder, const QString& modelDir) override;
    void submitUserMessage(const QString& userText) override;
    void clearConversation(const QString& modelFolder) override;
    void cancel() override;
    State state() const override { return m_state; }
    void submitToolResult(const QString& toolName, const QString& toolInput, const QString& result) override;
    bool isBusy() const { return m_busy; }

private Q_SLOTS:
    void onClientToken(const QString& token);
    void onClientFinished(const QString& fullText);
    void onClientError(const QString& message);
    void onClientToolCallRequested(const QString& toolName, const QString& toolInput, const QString& toolCallId);
    void onTtsFinished(const QString& outPath);
    void onTtsError(const QString& message);

private:
    void setState(State state);
    void refreshClientConfig();
    void refreshTtsConfig();
    void appendUserMessage(const QString& userText);
    void appendAiPlaceholder();
    void finalizeAssistantMessage(const QString& fullText);
    QJsonArray visibleMessagesForPersistence() const;
    QString cacheAudioPathForModel() const;

    IChatProvider* m_client{};
    IConversationStore* m_store{};
    ITtsProvider* m_tts{};
    ConversationRepositoryAdapter m_defaultStore;

    QString m_modelFolder;
    QString m_modelDir;

    bool m_busy{false};
    State m_state{State::Idle};
    bool m_hasAssistantPlaceholder{false};
    QJsonArray m_messages;

    // TTS streaming state
    QString m_pendingFinalText;
    QString m_pendingStreamText;
    QString m_pendingToolCallId;
    QString m_pendingToolName;
    QString m_pendingToolInput;
    QString m_pendingToolResult;
    bool m_hasPendingToolResult{false};
    bool m_ttsEnabled{false};
    bool m_waitingToolResult{false};
};
