#pragma once

#include "ai/core/IMcpAdapter.hpp"
#include "ai/core/McpServerConfig.hpp"

/**
 * HTTP/SSE based MCP adapter interface.
 * Inherits from IMcpAdapter to maintain compatibility.
 */
class IMcpHttpSseAdapter : public IMcpAdapter
{
public:
    virtual ~IMcpHttpSseAdapter() = default;

    // From IMcpAdapter
    bool isEnabled() const override = 0;
    bool ensureReady(QString* errorMessage = nullptr) override = 0;
    QJsonArray listTools(QString* errorMessage = nullptr) override = 0;
    QString callTool(const QString& toolName, const QString& toolInputJson, QString* errorMessage = nullptr) override = 0;

    // HTTP/SSE specific methods
    virtual bool setServerConfig(const McpServerConfig& config, QString* errorMessage = nullptr) = 0;
    virtual McpServerConfig serverConfig() const = 0;
};
