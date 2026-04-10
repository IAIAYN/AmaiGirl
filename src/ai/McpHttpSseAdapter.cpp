#include "ai/McpHttpSseAdapter.hpp"
#include "common/SettingsManager.hpp"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QEventLoop>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
#include <QDebug>

McpHttpSseAdapter::McpHttpSseAdapter(QObject* parent)
    : QObject(parent)
{
}

McpHttpSseAdapter::~McpHttpSseAdapter() = default;

bool McpHttpSseAdapter::isEnabled() const
{
    return m_config.enabled && !m_config.url.isEmpty();
}

bool McpHttpSseAdapter::ensureReady(QString* errorMessage)
{
    if (!isEnabled()) {
        if (errorMessage) *errorMessage = QStringLiteral("HTTP/SSE MCP adapter is not enabled");
        return false;
    }

    if (!validateConfig(errorMessage))
        return false;

    // Try to initialize by calling tools/list
    if (!m_initialized) {
        QJsonObject toolsListReq = buildRequestObject(1, QStringLiteral("tools/list"));
        QString result = performRequest(toolsListReq, errorMessage);
        if (result.isEmpty())
            return false;
        m_initialized = true;
    }

    return true;
}

QJsonArray McpHttpSseAdapter::listTools(QString* errorMessage)
{
    if (!ensureReady(errorMessage))
        return QJsonArray();

    QJsonObject toolsListReq = buildRequestObject(++m_requestIdCounter, QStringLiteral("tools/list"));
    QString result = performRequest(toolsListReq, errorMessage);

    if (result.isEmpty())
        return QJsonArray();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(result.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        if (errorMessage) *errorMessage = QStringLiteral("Failed to parse tools/list response: ") + err.errorString();
        return QJsonArray();
    }

    if (!doc.isObject()) {
        if (errorMessage) *errorMessage = QStringLiteral("tools/list response is not a JSON object");
        return QJsonArray();
    }

    QJsonObject respObj = doc.object();
    if (respObj.contains("result") && respObj.value("result").isObject()) {
        QJsonObject resultObj = respObj.value("result").toObject();
        if (resultObj.contains("tools") && resultObj.value("tools").isArray()) {
            return resultObj.value("tools").toArray();
        }
    }

    return QJsonArray();
}

QString McpHttpSseAdapter::callTool(const QString& toolName, const QString& toolInputJson, QString* errorMessage)
{
    if (!ensureReady(errorMessage))
        return QString();

    QJsonObject params;
    params["name"] = toolName;

    // Parse input as JSON object
    QJsonParseError err;
    QJsonDocument inputDoc = QJsonDocument::fromJson(toolInputJson.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && inputDoc.isObject()) {
        params["arguments"] = inputDoc.object();
    } else {
        // If not valid JSON, treat as string
        params["arguments"] = QJsonObject{{"input", toolInputJson}};
    }

    QJsonObject toolCallReq = buildRequestObject(++m_requestIdCounter, QStringLiteral("tools/call"), params);
    QString result = performRequest(toolCallReq, errorMessage);

    if (result.isEmpty())
        return QString();

    // Parse response to extract result
    QJsonDocument doc = QJsonDocument::fromJson(result.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        if (errorMessage) *errorMessage = QStringLiteral("Failed to parse tool result: ") + err.errorString();
        return QString();
    }

    if (doc.isObject()) {
        QJsonObject respObj = doc.object();
        if (respObj.contains("result") && respObj.value("result").isObject()) {
            return QJsonDocument(respObj.value("result").toObject()).toJson(QJsonDocument::Compact);
        }
    }

    return result;
}

bool McpHttpSseAdapter::setServerConfig(const McpServerConfig& config, QString* errorMessage)
{
    if (config.type != McpServerConfig::Type::HttpSSE) {
        if (errorMessage) *errorMessage = QStringLiteral("Config is not for HTTP/SSE type");
        return false;
    }

    if (!config.isValid()) {
        if (errorMessage) *errorMessage = config.validate();
        return false;
    }

    m_config = config;
    m_initialized = false;
    m_requestIdCounter = 0;

    return true;
}

McpServerConfig McpHttpSseAdapter::serverConfig() const
{
    return m_config;
}

bool McpHttpSseAdapter::validateConfig(QString* errorMessage) const
{
    if (!m_config.isValid()) {
        if (errorMessage) *errorMessage = m_config.validate();
        return false;
    }

    QUrl url(m_config.url);
    if (!url.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral("Invalid HTTP/SSE URL: ") + m_config.url;
        return false;
    }

    return true;
}

QJsonObject McpHttpSseAdapter::buildRequestObject(int requestId, const QString& method, const QJsonObject& params) const
{
    QJsonObject req;
    req["jsonrpc"] = QStringLiteral("2.0");
    req["id"] = requestId;
    req["method"] = method;
    req["params"] = params;
    return req;
}

QString McpHttpSseAdapter::performRequest(const QJsonObject& request, QString* errorMessage)
{
    QUrl url(m_config.url);
    QNetworkRequest networkReq(url);
    networkReq.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    // Add custom headers from config
    for (auto it = m_config.headers.begin(); it != m_config.headers.end(); ++it) {
        networkReq.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    // Perform synchronous request
    QEventLoop eventLoop;
    QNetworkReply* reply = m_networkManager.post(networkReq, QJsonDocument(request).toJson(QJsonDocument::Compact));

    if (!reply) {
        if (errorMessage) *errorMessage = QStringLiteral("Failed to create network request");
        return QString();
    }

    // Wait for response (with timeout consideration from config)
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(m_config.timeoutMs);
    QObject::connect(&timer, &QTimer::timeout, &eventLoop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);

    timer.start();
    eventLoop.exec();
    timer.stop();

    QString result;
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("HTTP request failed: ") + reply->errorString();
            if (statusCode != 0) {
                *errorMessage += QStringLiteral(" (HTTP ") + QString::number(statusCode) + QStringLiteral(")");
            }
        }
    } else if (statusCode >= 400) {
        if (errorMessage) *errorMessage = QStringLiteral("HTTP error: ") + QString::number(statusCode);
    } else {
        result = QString::fromUtf8(reply->readAll());
    }

    reply->deleteLater();
    return result;
}
