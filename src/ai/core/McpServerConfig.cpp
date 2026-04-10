#include "ai/core/McpServerConfig.hpp"
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>

#include <algorithm>

namespace {
QStringList splitArgs(const QString& raw)
{
    QStringList out;
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    for (const QString& token : raw.split(ws, Qt::SkipEmptyParts))
        out.push_back(token);
    return out;
}
} // namespace

McpServerConfig::McpServerConfig(const QString& name, Type type)
    : name(name), type(type)
{
}

QJsonObject McpServerConfig::toJson() const
{
    QJsonObject obj;
    obj["type"] = typeToString(type);

    if (type == Type::Stdio) {
        obj["command"] = command;
        QJsonArray argsArr;
        const QStringList argsList = splitArgs(args);
        for (const QString& token : argsList)
            argsArr.append(token);
        obj["args"] = argsArr;

        QJsonObject envObj;
        for (auto it = env.begin(); it != env.end(); ++it)
            envObj.insert(it.key(), it.value());
        obj["env"] = envObj;
    } else if (type == Type::HttpSSE) {
        obj["url"] = url;
        QJsonObject headersObj;
        for (auto it = headers.begin(); it != headers.end(); ++it) {
            headersObj[it.key()] = it.value();
        }
        obj["headers"] = headersObj;
    }

    return obj;
}

McpServerConfig McpServerConfig::fromJson(const QJsonObject& obj)
{
    McpServerConfig config;
    config.name = obj.value("name").toString();
    config.type = stringToType(obj.value("type").toString("stdio"));
    config.enabled = true;
    config.timeoutMs = std::max(1000, obj.value("timeoutMs").toInt(8000));

    if (config.type == Type::Stdio) {
        config.command = obj.value("command").toString();
        if (obj.value("args").isArray())
        {
            QStringList argsList;
            const QJsonArray argsArr = obj.value("args").toArray();
            for (const QJsonValue& val : argsArr)
            {
                if (val.isString())
                    argsList.push_back(val.toString());
            }
            config.args = argsList.join(' ');
        }
        else
        {
            config.args = obj.value("args").toString();
        }

        if (obj.value("env").isObject())
        {
            const QJsonObject envObj = obj.value("env").toObject();
            for (auto it = envObj.begin(); it != envObj.end(); ++it)
                config.env.insert(it.key(), it.value().toString());
        }
    } else if (config.type == Type::HttpSSE) {
        config.url = obj.value("url").toString();
        const QJsonObject headersObj = obj.value("headers").toObject();
        for (auto it = headersObj.begin(); it != headersObj.end(); ++it) {
            config.headers[it.key()] = it.value().toString();
        }
    }

    return config;
}

QString McpServerConfig::typeToString(Type t)
{
    switch (t) {
    case Type::Stdio:
        return QStringLiteral("stdio");
    case Type::HttpSSE:
        return QStringLiteral("http-sse");
    }
    return QStringLiteral("stdio");
}

McpServerConfig::Type McpServerConfig::stringToType(const QString& s)
{
    const QString normalized = s.toLower().trimmed();
    if (normalized == QStringLiteral("http-sse") || normalized == QStringLiteral("httpsse")) {
        return Type::HttpSSE;
    }
    return Type::Stdio;
}

bool McpServerConfig::isValid() const
{
    return validate().isEmpty();
}

QString McpServerConfig::validate() const
{
    if (name.trimmed().isEmpty()) {
        return QStringLiteral("Server name cannot be empty");
    }

    if (type == Type::Stdio) {
        if (command.trimmed().isEmpty()) {
            return QStringLiteral("Command cannot be empty for Stdio type");
        }
    } else if (type == Type::HttpSSE) {
        if (url.trimmed().isEmpty()) {
            return QStringLiteral("URL cannot be empty for HTTP/SSE type");
        }
    }

    return QString();
}
