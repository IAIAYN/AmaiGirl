#pragma once

#include "ai/core/IMcpAdapter.hpp"

#include <QObject>
#include <QProcess>
#include <QJsonObject>

class McpStdioAdapter final : public QObject, public IMcpAdapter
{
    Q_OBJECT
public:
    explicit McpStdioAdapter(QObject* parent = nullptr);
    ~McpStdioAdapter() override;

    bool isEnabled() const override;
    bool ensureReady(QString* errorMessage = nullptr) override;
    QJsonArray listTools(QString* errorMessage = nullptr) override;
    QString callTool(const QString& toolName, const QString& toolInputJson, QString* errorMessage = nullptr) override;

    // Configuration
    bool setConfig(const class McpServerConfig& config, QString* errorMessage = nullptr);

private:
    void reloadConfig();
    bool startProcess(QString* errorMessage);
    bool sendRequest(const QString& method, const QJsonObject& params, QJsonObject* result, QString* errorMessage, int timeoutMs);
    bool sendNotification(const QString& method, const QJsonObject& params, QString* errorMessage);
    bool drainFrames(int timeoutMs, int expectedId, QJsonObject* matched, QString* errorMessage);
    bool parseOneFrame(QJsonObject* frame);
    void writeFrame(const QJsonObject& obj);

    QString m_program;
    QStringList m_args;
    int m_timeoutMs{8000};
    bool m_initialized{false};
    int m_nextId{1};

    QProcess m_process;
    QByteArray m_readBuffer;
    QJsonArray m_cachedTools;
    bool m_useContentLengthFraming{true};
};
