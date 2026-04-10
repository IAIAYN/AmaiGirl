#include "ai/McpStdioAdapter.hpp"

#include "common/SettingsManager.hpp"
#include "ai/core/McpServerConfig.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QElapsedTimer>

#include <algorithm>

namespace {
QString envTrimmed(const char* key)
{
    return QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(key)).trimmed();
}

QStringList splitArgs(const QString& raw)
{
    return QProcess::splitCommand(raw);
}

bool shouldFallbackToJsonLineMode(const QString& message)
{
    const QString m = message.toLower();
    return m.contains(QStringLiteral("content-length"))
        && (m.contains(QStringLiteral("json_invalid")) || m.contains(QStringLiteral("invalid json")));
}
} // namespace

McpStdioAdapter::McpStdioAdapter(QObject* parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    reloadConfig();
}

McpStdioAdapter::~McpStdioAdapter()
{
    if (m_process.state() == QProcess::NotRunning)
        return;

    m_process.terminate();
    if (!m_process.waitForFinished(800))
    {
        m_process.kill();
        m_process.waitForFinished(800);
    }
}

void McpStdioAdapter::reloadConfig()
{
    // Legacy method: no longer loads from SettingsManager (which no longer has mcpCommand/Args)
    // Configuration should now be set via setConfig() method.
    // Keep this method for minimal backward compatibility but make it a no-op.
}

bool McpStdioAdapter::setConfig(const McpServerConfig& config, QString* errorMessage)
{
    if (config.type != McpServerConfig::Type::Stdio) {
        if (errorMessage) *errorMessage = QStringLiteral("Config is not for Stdio type");
        return false;
    }

    if (!config.isValid()) {
        if (errorMessage) *errorMessage = config.validate();
        return false;
    }

    if (config.enabled)
    {
        const QString commandText = config.command.trimmed();
        const QString argsText = config.args.trimmed();

        if (argsText.isEmpty())
        {
            const QStringList commandTokens = QProcess::splitCommand(commandText);
            if (!commandTokens.isEmpty())
            {
                m_program = commandTokens.first();
                m_args = commandTokens.mid(1);
            }
            else
            {
                m_program = commandText;
                m_args.clear();
            }
        }
        else
        {
            m_program = commandText;
            m_args = splitArgs(argsText);
        }
    }
    else
    {
        m_program.clear();
        m_args.clear();
    }

    m_timeoutMs = std::max(1000, config.timeoutMs);
    m_useContentLengthFraming = true;

    QProcessEnvironment processEnv = QProcessEnvironment::systemEnvironment();
    for (auto it = config.env.begin(); it != config.env.end(); ++it)
        processEnv.insert(it.key(), it.value());
    m_process.setProcessEnvironment(processEnv);

    // Kill existing process if config changed
    if (m_process.state() == QProcess::Running) {
        m_process.terminate();
        if (!m_process.waitForFinished(800))
        {
            m_process.kill();
            m_process.waitForFinished(800);
        }
        m_initialized = false;
        m_readBuffer.clear();
        m_cachedTools = QJsonArray{};
    }

    return true;
}

bool McpStdioAdapter::isEnabled() const
{
    return !m_program.isEmpty();
}

bool McpStdioAdapter::startProcess(QString* errorMessage)
{
    if (m_process.state() == QProcess::Running)
        return true;

    m_process.setProgram(m_program);
    m_process.setArguments(m_args);
    m_process.start();

    if (!m_process.waitForStarted(m_timeoutMs))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("MCP process start failed: %1").arg(m_process.errorString());
        return false;
    }
    return true;
}

bool McpStdioAdapter::ensureReady(QString* errorMessage)
{
    reloadConfig();

    if (!isEnabled())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("MCP is disabled (AMAI_MCP_COMMAND is empty)");
        return false;
    }

    if (m_initialized)
        return true;

    if (!startProcess(errorMessage))
        return false;

    QJsonObject initParams;
    initParams.insert(QStringLiteral("protocolVersion"), QStringLiteral("2024-11-05"));
    initParams.insert(QStringLiteral("capabilities"), QJsonObject{});
    QJsonObject clientInfo;
    clientInfo.insert(QStringLiteral("name"), QStringLiteral("AmaiGirl"));
    clientInfo.insert(QStringLiteral("version"), QStringLiteral("0.1"));
    initParams.insert(QStringLiteral("clientInfo"), clientInfo);

    QJsonObject initResult;
    QString initError;
    if (!sendRequest(QStringLiteral("initialize"), initParams, &initResult, &initError, m_timeoutMs))
    {
        if (m_useContentLengthFraming && shouldFallbackToJsonLineMode(initError))
        {
            m_useContentLengthFraming = false;
            m_readBuffer.clear();

            if (m_process.state() == QProcess::Running)
            {
                m_process.terminate();
                if (!m_process.waitForFinished(800))
                {
                    m_process.kill();
                    m_process.waitForFinished(800);
                }
            }

            if (!startProcess(&initError)
                || !sendRequest(QStringLiteral("initialize"), initParams, &initResult, &initError, m_timeoutMs))
            {
                if (errorMessage)
                    *errorMessage = initError;
                return false;
            }
        }
        else
        {
            if (errorMessage)
                *errorMessage = initError;
            return false;
        }
    }

    if (!sendNotification(QStringLiteral("notifications/initialized"), QJsonObject{}, errorMessage))
        return false;

    m_initialized = true;
    return true;
}

QJsonArray McpStdioAdapter::listTools(QString* errorMessage)
{
    if (!ensureReady(errorMessage))
        return QJsonArray{};

    QJsonObject result;
    if (!sendRequest(QStringLiteral("tools/list"), QJsonObject{}, &result, errorMessage, m_timeoutMs))
        return QJsonArray{};

    m_cachedTools = result.value(QStringLiteral("tools")).toArray();
    return m_cachedTools;
}

QString McpStdioAdapter::callTool(const QString& toolName, const QString& toolInputJson, QString* errorMessage)
{
    if (!ensureReady(errorMessage))
        return QString();

    QJsonParseError parseError;
    const QJsonDocument argsDoc = QJsonDocument::fromJson(toolInputJson.toUtf8(), &parseError);
    QJsonObject argsObj;
    if (parseError.error == QJsonParseError::NoError && argsDoc.isObject())
        argsObj = argsDoc.object();

    QJsonObject params;
    params.insert(QStringLiteral("name"), toolName);
    params.insert(QStringLiteral("arguments"), argsObj);

    QJsonObject result;
    if (!sendRequest(QStringLiteral("tools/call"), params, &result, errorMessage, m_timeoutMs))
        return QString();

    const QJsonArray content = result.value(QStringLiteral("content")).toArray();
    QStringList textParts;
    for (const QJsonValue& v : content)
    {
        const QJsonObject item = v.toObject();
        if (item.value(QStringLiteral("type")).toString() == QStringLiteral("text"))
        {
            const QString text = item.value(QStringLiteral("text")).toString();
            if (!text.isEmpty())
                textParts.push_back(text);
        }
    }

    if (!textParts.isEmpty())
        return textParts.join(QStringLiteral("\n"));

    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

bool McpStdioAdapter::sendNotification(const QString& method, const QJsonObject& params, QString* errorMessage)
{
    if (m_process.state() != QProcess::Running)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("MCP process is not running");
        return false;
    }

    QJsonObject req;
    req.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    req.insert(QStringLiteral("method"), method);
    req.insert(QStringLiteral("params"), params);
    writeFrame(req);
    if (!m_process.waitForBytesWritten(m_timeoutMs))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("MCP notification write timeout");
        return false;
    }
    return true;
}

bool McpStdioAdapter::sendRequest(const QString& method, const QJsonObject& params, QJsonObject* result, QString* errorMessage, int timeoutMs)
{
    if (m_process.state() != QProcess::Running)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("MCP process is not running");
        return false;
    }

    const int reqId = m_nextId++;

    QJsonObject req;
    req.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    req.insert(QStringLiteral("id"), reqId);
    req.insert(QStringLiteral("method"), method);
    req.insert(QStringLiteral("params"), params);
    writeFrame(req);

    if (!m_process.waitForBytesWritten(timeoutMs))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("MCP request write timeout");
        return false;
    }

    QJsonObject matched;
    if (!drainFrames(timeoutMs, reqId, &matched, errorMessage))
        return false;

    if (matched.contains(QStringLiteral("error")))
    {
        if (errorMessage)
            *errorMessage = QString::fromUtf8(QJsonDocument(matched.value(QStringLiteral("error")).toObject()).toJson(QJsonDocument::Compact));
        return false;
    }

    if (result)
        *result = matched.value(QStringLiteral("result")).toObject();
    return true;
}

bool McpStdioAdapter::drainFrames(int timeoutMs, int expectedId, QJsonObject* matched, QString* errorMessage)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs)
    {
        if (m_process.bytesAvailable() <= 0)
            m_process.waitForReadyRead(50);

        m_readBuffer += m_process.readAllStandardOutput();

        QJsonObject frame;
        while (parseOneFrame(&frame))
        {
            if (!frame.contains(QStringLiteral("id")))
                continue;

            const int id = frame.value(QStringLiteral("id")).toInt(-1);
            if (id == expectedId)
            {
                if (matched)
                    *matched = frame;
                return true;
            }
        }
    }

    if (errorMessage)
    {
        const QByteArray stderrOut = m_process.readAllStandardError();
        *errorMessage = QStringLiteral("MCP request timeout") +
            (stderrOut.isEmpty() ? QString() : QStringLiteral(": ") + QString::fromUtf8(stderrOut));
    }
    return false;
}

bool McpStdioAdapter::parseOneFrame(QJsonObject* frame)
{
    if (!m_useContentLengthFraming)
    {
        const int lineEnd = m_readBuffer.indexOf('\n');
        if (lineEnd < 0)
            return false;

        QByteArray line = m_readBuffer.left(lineEnd);
        m_readBuffer.remove(0, lineEnd + 1);
        line = line.trimmed();
        if (line.isEmpty())
            return false;

        QJsonParseError lineError;
        const QJsonDocument lineDoc = QJsonDocument::fromJson(line, &lineError);
        if (lineError.error != QJsonParseError::NoError || !lineDoc.isObject())
            return false;

        if (frame)
            *frame = lineDoc.object();
        return true;
    }

    int sepLen = 0;
    int headerEnd = m_readBuffer.indexOf("\r\n\r\n");
    if (headerEnd >= 0)
    {
        sepLen = 4;
    }
    else
    {
        headerEnd = m_readBuffer.indexOf("\n\n");
        if (headerEnd >= 0)
            sepLen = 2;
    }

    if (headerEnd < 0)
        return false;

    const QByteArray header = m_readBuffer.left(headerEnd);
    int contentLength = -1;
    const QList<QByteArray> lines = header.split('\n');
    for (QByteArray line : lines)
    {
        line = line.trimmed();
        const QByteArray key("content-length:");
        if (line.toLower().startsWith(key))
        {
            bool ok = false;
            contentLength = line.mid(key.size()).trimmed().toInt(&ok);
            if (!ok)
                contentLength = -1;
        }
    }

    if (contentLength < 0)
    {
        // Discard malformed header block and continue scanning.
        m_readBuffer.remove(0, headerEnd + sepLen);
        return false;
    }

    const int bodyStart = headerEnd + sepLen;
    if (m_readBuffer.size() < bodyStart + contentLength)
        return false;

    const QByteArray body = m_readBuffer.mid(bodyStart, contentLength);
    m_readBuffer.remove(0, bodyStart + contentLength);

    QJsonParseError e;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    if (frame)
        *frame = doc.object();
    return true;
}

void McpStdioAdapter::writeFrame(const QJsonObject& obj)
{
    const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    if (!m_useContentLengthFraming)
    {
        m_process.write(body);
        m_process.write("\n");
        return;
    }

    QByteArray packet;
    packet += "Content-Length: ";
    packet += QByteArray::number(body.size());
    packet += "\r\n\r\n";
    packet += body;
    m_process.write(packet);
}
