#pragma once

#include "core/IMcpHttpSseAdapter.hpp"
#include <QObject>
#include <QNetworkAccessManager>
#include <QJsonArray>
#include <QJsonObject>

/**
 * HTTP/SSE based MCP adapter implementation.
 * Communicates with MCP servers over HTTP with SSE support for streaming.
 */
class McpHttpSseAdapter : public QObject, public IMcpHttpSseAdapter
{
    Q_OBJECT
public:
    explicit McpHttpSseAdapter(QObject* parent = nullptr);
    ~McpHttpSseAdapter();

    // IMcpAdapter implementation
    bool isEnabled() const override;
    bool ensureReady(QString* errorMessage = nullptr) override;
    QJsonArray listTools(QString* errorMessage = nullptr) override;
    QString callTool(const QString& toolName, const QString& toolInputJson, QString* errorMessage = nullptr) override;

    // IMcpHttpSseAdapter implementation
    bool setServerConfig(const McpServerConfig& config, QString* errorMessage = nullptr) override;
    McpServerConfig serverConfig() const override;

private:
    McpServerConfig m_config;
    QNetworkAccessManager m_networkManager;
    bool m_initialized{false};
    int m_requestIdCounter{0};

    // Helper methods
    bool validateConfig(QString* errorMessage = nullptr) const;
    QJsonObject buildRequestObject(int requestId, const QString& method, const QJsonObject& params = QJsonObject()) const;
    QString performRequest(const QJsonObject& request, QString* errorMessage = nullptr);
};
