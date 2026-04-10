#pragma once

#include <QObject>
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QString>
#include <QUrl>

#include "ai/core/ITtsProvider.hpp"

// NOTE: Do NOT undefine Qt's 'slots' keyword macro here.
// If some 3rd-party code defines a conflicting macro named 'slots', we clean it up
// in our .cpp translation units before including Qt headers.

class OpenAITtsClient : public QObject, public ITtsProvider
{
    Q_OBJECT
public:
    using TtsConfig = ITtsProvider::TtsConfig;

    explicit OpenAITtsClient(QObject* parent = nullptr);

    void setConfig(TtsConfig cfg) override;
    TtsConfig config() const override;

    bool isBusy() const override { return m_reply != nullptr; }

    // Writes audio to outPath (overwrites) and emits finished(outPath).
    void startSpeech(const QString& text, const QString& outPath) override;
    void cancel() override;

Q_SIGNALS:
    void started();
    void finished(const QString& outPath);
    void errorOccurred(const QString& message);

private Q_SLOTS:
    void onFinished();

private:
    static QString normalizeBaseUrlToV1(const QString& baseUrl);
    static QUrl makeSpeechUrl(const QString& baseUrlV1);

    void emitErrorAndCleanup(const QString& message);
    void cleanupReply();

    TtsConfig m_cfg;
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;

    QString m_outPath;
};
