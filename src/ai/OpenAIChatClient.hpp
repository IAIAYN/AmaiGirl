#pragma once

#include <QObject>
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QString>
#include <QJsonArray>
#include <QHash>

#include "ai/core/IChatProvider.hpp"

class OpenAIChatClient : public QObject, public IChatProvider
{
    Q_OBJECT
public:
    using ChatConfig = IChatProvider::ChatConfig;

    explicit OpenAIChatClient(QObject* parent = nullptr);

    void setConfig(ChatConfig cfg) override;
    ChatConfig config() const override;

    bool isBusy() const override { return m_reply != nullptr; }

    // messagesJson: already in simplified format we use in chat persistence.
    // [{"role":"user","content":"..."}, ...]
    void startChat(const QByteArray& messagesJson) override;
    void cancel() override;

    // OpenAI-compatible tools schema list (optional).
    void setTools(const QJsonArray& tools);

Q_SIGNALS:
    void started();
    void tokenReceived(const QString& text);
    void toolCallRequested(const QString& toolName, const QString& toolInput, const QString& toolCallId);
    void finished(const QString& fullText);
    void errorOccurred(const QString& message);

private Q_SLOTS:
    void onReadyRead();
    void onFinished();

private:
    static QString normalizeBaseUrl(const QString& baseUrl); // ensure ends with /v1
    static QUrl makeCompletionsUrl(const QString& baseUrl);

    ChatConfig m_cfg;
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;

    QByteArray m_streamBuffer;
    QByteArray m_nonStreamBuffer;
    QString m_accumulated;
    QJsonArray m_tools;
    QHash<int, QString> m_streamToolNameByIndex;
    QHash<int, QString> m_streamToolArgsByIndex;
    QHash<int, QString> m_streamToolIdByIndex;

    void emitErrorAndCleanup(const QString& message);
    void cleanupReply();

    void consumeSseLines(const QByteArray& chunk);
    void handleSseLine(const QByteArray& line);
};
