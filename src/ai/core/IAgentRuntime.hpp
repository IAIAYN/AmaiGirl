#pragma once

#include <QObject>
#include <QString>

class IAgentRuntime : public QObject
{
    Q_OBJECT
public:
    enum class State
    {
        Idle,
        Submitting,
        Streaming,
        Completed,
        Failed
    };

    explicit IAgentRuntime(QObject* parent = nullptr) : QObject(parent) {}
    ~IAgentRuntime() override = default;

    virtual void setModelContext(const QString& modelFolder, const QString& modelDir) = 0;
    virtual void submitUserMessage(const QString& userText) = 0;
    virtual void clearConversation(const QString& modelFolder) = 0;
    virtual void cancel() = 0;
    virtual State state() const = 0;
    virtual void setTtsProvider(class ITtsProvider* provider) = 0;
    virtual void submitToolResult(const QString& toolName, const QString& toolInput, const QString& result) = 0;

Q_SIGNALS:
    void stateChanged(IAgentRuntime::State state);
    void requestAppendUserMessage(const QString& text);
    void requestAppendAiMessageStart();
    void requestAppendAiToken(const QString& token);
    void requestFinalizeAssistantMessage(const QString& fullText, bool ensureBubbleExists);
    void requestSetBusy(bool busy);
    void requestLoadConversation(const QString& modelFolder);
    void requestConversationCleared(const QString& modelFolder);
    void requestStartTts(const QString& text, const QString& audioPath, bool streamingEnabled);
    void requestStartPlayback(const QString& audioPath);
    void requestStartPacedTextReveal(const QString& fullText);
    void requestToolCall(const QString& toolName, const QString& toolInput);
    void tokenReceived(const QString& token);
    void finished(const QString& fullText);
    void errorOccurred(const QString& message);
    void ttsFinished(const QString& audioPath);
    void ttsError(const QString& errorMessage);
    void toolCallRequested(const QString& toolName, const QString& toolInput);
};
