#pragma once

#include <QJsonArray>
#include <QString>
#include <memory>

class McpServerConfig;

class IMcpAdapter
{
public:
    virtual ~IMcpAdapter() = default;

    virtual bool isEnabled() const = 0;
    virtual bool ensureReady(QString* errorMessage = nullptr) = 0;
    virtual QJsonArray listTools(QString* errorMessage = nullptr) = 0;
    virtual QString callTool(const QString& toolName, const QString& toolInputJson, QString* errorMessage = nullptr) = 0;

    // Factory method to create appropriate adapter based on config type
    static std::unique_ptr<IMcpAdapter> create(const McpServerConfig& config, QString* errorMessage = nullptr);
};
