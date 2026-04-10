#pragma once

#include <QString>
#include <QMap>
#include <QJsonObject>

/**
 * Configuration for a single MCP (Model Context Protocol) server.
 * Supports both Stdio and HTTP/SSE connection types.
 */
class McpServerConfig
{
public:
    enum class Type {
        Stdio,      // stdio based MCP (command + args)
        HttpSSE     // HTTP/SSE based MCP (url + headers)
    };

    McpServerConfig() = default;
    explicit McpServerConfig(const QString& name, Type type = Type::Stdio);

    // Basic properties
    QString name;           // unique identifier
    Type type = Type::Stdio;
    bool enabled = true;
    int timeoutMs = 8000;   // minimum 1000ms

    // Stdio-specific properties
    QString command;        // e.g., npx, python
    QString args;           // space-separated arguments
    QMap<QString, QString> env;

    // HTTP/SSE-specific properties
    QString url;            // e.g., http://localhost:3000/mcp
    QMap<QString, QString> headers;  // custom headers (e.g., Authorization, Custom-Token)

    // Serialization
    QJsonObject toJson() const;
    static McpServerConfig fromJson(const QJsonObject& obj);

    // Type conversion
    static QString typeToString(Type t);
    static Type stringToType(const QString& s);

    // Validation
    bool isValid() const;
    QString validate() const;  // returns empty string if valid, error message otherwise
};

Q_DECLARE_METATYPE(McpServerConfig)
Q_DECLARE_METATYPE(McpServerConfig::Type)
