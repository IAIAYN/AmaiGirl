#pragma once

#include "ai/core/McpServerStatus.hpp"

#include <QMainWindow>
#include <QScopedPointer>
#include <QString>

class QEvent;

class ChatWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit ChatWindow(QWidget* parent = nullptr);
    ~ChatWindow() override;

    // called when model changes
    void setCurrentModel(const QString& modelFolder, const QString& modelDir);

    // Returns the current assistant draft (latest assistant bubble content) for fallback alignment.
    QString currentAssistantDraft() const;

    // Commit an assistant message with final content (used by controller when it needs to finalize immediately).
    void finalizeAssistantMessage(const QString& content);

    // Replace the current assistant bubble content and mark it as final (no further streaming expected).
    // This is used when we delay text reveal to pace with TTS and need a single authoritative final message.
    void finalizeAssistantMessage(const QString& content, bool ensureBubbleExists);

Q_SIGNALS:
    void requestSendMessage(const QString& modelFolder, const QString& userText);
    void requestAbortCurrentTask();
    void requestClearChat(const QString& modelFolder);
    void requestRefreshMcpServerStatuses();
    void requestSetMcpServerEnabled(const QString& serverName, bool enabled);

public Q_SLOTS:
    void setPersistenceEnabled(bool enabled);
    void setMcpServerStatuses(const QList<McpServerStatus>& statuses);
    void setBusy(bool busy);
    void sealCurrentAiBubble();
    void cancelCurrentAiDraftBubble();
    void appendUserMessage(const QString& text);
    void appendAiMessageStart();
    void appendAiToken(const QString& token);
    void appendAiMessageFinish();
    void setAiMessageContent(const QString& content);
    void loadFromDisk(const QString& modelFolder);

protected:
    bool event(QEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    class Impl;
    QScopedPointer<Impl> d;
};
