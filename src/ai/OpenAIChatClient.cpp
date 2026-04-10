#include "ai/OpenAIChatClient.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QSet>
#include <QUrl>

namespace {
constexpr int kHttpTimeoutMs = 120000;

QString withErrorTag(const QString& message)
{
    if (message.startsWith(QStringLiteral("[Error]"))) return message;
    return QStringLiteral("[Error] %1").arg(message);
}

QJsonArray normalizeMessagesForProvider(const QJsonArray& messages)
{
    QJsonArray normalized;

    auto normalizeTextRoleMessage = [](const QJsonObject& src, const QString& role) {
        QJsonObject out;
        out.insert(QStringLiteral("role"), role);
        if (src.value(QStringLiteral("content")).isString())
            out.insert(QStringLiteral("content"), src.value(QStringLiteral("content")).toString());
        return out;
    };

    for (int i = 0; i < messages.size(); ++i)
    {
        const QJsonValue item = messages.at(i);
        if (!item.isObject())
            continue;

        const QJsonObject src = item.toObject();
        const QString role = src.value(QStringLiteral("role")).toString();
        if (role.isEmpty())
            continue;

        if (role == QStringLiteral("assistant") && src.value(QStringLiteral("tool_calls")).isArray())
        {
            const QJsonArray rawToolCalls = src.value(QStringLiteral("tool_calls")).toArray();
            QJsonArray sanitizedToolCalls;
            QSet<QString> expectedIds;
            for (const QJsonValue& tcVal : rawToolCalls)
            {
                if (!tcVal.isObject())
                    continue;

                const QJsonObject tcObj = tcVal.toObject();
                const QString id = tcObj.value(QStringLiteral("id")).toString().trimmed();
                const QJsonObject fn = tcObj.value(QStringLiteral("function")).toObject();
                const QString fnName = fn.value(QStringLiteral("name")).toString().trimmed();
                if (id.isEmpty() || fnName.isEmpty())
                    continue;

                QJsonObject fnObj;
                fnObj.insert(QStringLiteral("name"), fnName);
                fnObj.insert(QStringLiteral("arguments"), fn.value(QStringLiteral("arguments")).toString());

                QJsonObject one;
                one.insert(QStringLiteral("id"), id);
                one.insert(QStringLiteral("type"), QStringLiteral("function"));
                one.insert(QStringLiteral("function"), fnObj);
                sanitizedToolCalls.append(one);
                expectedIds.insert(id);
            }

            if (sanitizedToolCalls.isEmpty())
            {
                normalized.append(normalizeTextRoleMessage(src, role));
                continue;
            }

            QJsonArray followingTools;
            QSet<QString> seenIds;
            int j = i + 1;
            while (j < messages.size() && seenIds.size() < expectedIds.size())
            {
                const QJsonValue nextVal = messages.at(j);
                if (!nextVal.isObject())
                    break;

                const QJsonObject nextObj = nextVal.toObject();
                const QString nextRole = nextObj.value(QStringLiteral("role")).toString();
                if (nextRole != QStringLiteral("tool"))
                    break;

                const QString toolCallId = nextObj.value(QStringLiteral("tool_call_id")).toString().trimmed();
                if (toolCallId.isEmpty() || !expectedIds.contains(toolCallId) || seenIds.contains(toolCallId))
                {
                    ++j;
                    continue;
                }

                QJsonObject toolMsg;
                toolMsg.insert(QStringLiteral("role"), QStringLiteral("tool"));
                toolMsg.insert(QStringLiteral("tool_call_id"), toolCallId);
                toolMsg.insert(QStringLiteral("content"), nextObj.value(QStringLiteral("content")).toString());
                followingTools.append(toolMsg);
                seenIds.insert(toolCallId);
                ++j;
            }

            if (seenIds.size() == expectedIds.size())
            {
                QJsonObject assistantMsg = normalizeTextRoleMessage(src, role);
                assistantMsg.insert(QStringLiteral("tool_calls"), sanitizedToolCalls);
                normalized.append(assistantMsg);
                for (const QJsonValue& toolMsg : followingTools)
                    normalized.append(toolMsg);
                i = j - 1;
            }
            else
            {
                // Invalid pair in history: keep assistant content but strip tool_calls to satisfy strict providers.
                normalized.append(normalizeTextRoleMessage(src, role));
            }
            continue;
        }

        if (role == QStringLiteral("tool"))
        {
            // Keep only tool messages that are already consumed as part of a validated
            // assistant(tool_calls)->tool pair sequence above.
            // Standalone/legacy tool messages can cause strict providers to reject requests.
            continue;
        }

        normalized.append(normalizeTextRoleMessage(src, role));
    }
    return normalized;
}
} // namespace

OpenAIChatClient::OpenAIChatClient(QObject* parent)
    : QObject(parent)
{
}

void OpenAIChatClient::setConfig(ChatConfig cfg)
{
    m_cfg = std::move(cfg);
}

OpenAIChatClient::ChatConfig OpenAIChatClient::config() const
{
    return m_cfg;
}

void OpenAIChatClient::setTools(const QJsonArray& tools)
{
    m_tools = tools;
}

QString OpenAIChatClient::normalizeBaseUrl(const QString& baseUrl)
{
    QString u = baseUrl.trimmed();
    while (u.endsWith('/')) u.chop(1);
    // user will input https://xxx/v1
    if (!u.endsWith("/v1"))
    {
        // if they accidentally passed https://xxx, keep consistent with requirement: only autocomplete /chat/completions
        // so we don't alter to /v1.
        return u;
    }
    return u;
}

QUrl OpenAIChatClient::makeCompletionsUrl(const QString& baseUrl)
{
    QString u = normalizeBaseUrl(baseUrl);
    // Requirement: only autocomplete /chat/completions
    if (!u.endsWith("/chat/completions"))
        u += "/chat/completions";
    return QUrl(u);
}

void OpenAIChatClient::startChat(const QByteArray& messagesJson)
{
    if (isBusy())
    {
        emitErrorAndCleanup(QStringLiteral("Request already in progress"));
        return;
    }

    if (m_cfg.baseUrl.trimmed().isEmpty())
    {
        emit errorOccurred(withErrorTag(tr("base_url 为空")));
        return;
    }

    // api_key is allowed to be empty (some providers use IP allow-list or other auth).

    QJsonParseError parseErr;
    const auto msgDoc = QJsonDocument::fromJson(messagesJson, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !msgDoc.isArray())
    {
        emit errorOccurred(withErrorTag(tr("消息体格式错误")));
        return;
    }

    QJsonArray messages = normalizeMessagesForProvider(msgDoc.array());
    if (!m_cfg.systemPrompt.trimmed().isEmpty())
    {
        QJsonObject sys;
        sys["role"] = "system";
        sys["content"] = m_cfg.systemPrompt;
        QJsonArray withSys;
        withSys.append(sys);
        for (const auto& v : messages) withSys.append(v);
        messages = withSys;
    }

    QJsonObject body;
    body["model"] = m_cfg.model.isEmpty() ? QStringLiteral("gpt-4o-mini") : m_cfg.model;
    body["messages"] = messages;
    body["stream"] = m_cfg.stream;
    if (!m_tools.isEmpty())
    {
        body["tools"] = m_tools;
    }

    QNetworkRequest req(makeCompletionsUrl(m_cfg.baseUrl));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_cfg.apiKey.trimmed().isEmpty())
    {
        req.setRawHeader("Authorization", QByteArray("Bearer ") + m_cfg.apiKey.trimmed().toUtf8());
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    req.setTransferTimeout(kHttpTimeoutMs);
#endif

    m_accumulated.clear();
    m_streamBuffer.clear();
    m_nonStreamBuffer.clear();
    m_streamToolNameByIndex.clear();
    m_streamToolArgsByIndex.clear();
    m_streamToolIdByIndex.clear();

    m_reply = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    if (!m_reply)
    {
        emit errorOccurred(withErrorTag(tr("无法发起请求")));
        return;
    }

    m_reply->setReadBufferSize(0);

    connect(m_reply, &QNetworkReply::readyRead, this, &OpenAIChatClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &OpenAIChatClient::onFinished);

    emit started();
}

void OpenAIChatClient::cancel()
{
    if (!m_reply) return;
    m_reply->abort();
    cleanupReply();
}

void OpenAIChatClient::onReadyRead()
{
    if (!m_reply) return;

    const QByteArray chunk = m_reply->readAll();
    if (chunk.isEmpty()) return;

    m_nonStreamBuffer += chunk;

    if (m_cfg.stream)
    {
        consumeSseLines(chunk);
    }
}

void OpenAIChatClient::onFinished()
{
    if (!m_reply) return;

    if (m_reply->error() != QNetworkReply::NoError)
    {
        QString err = m_reply->errorString();
        const int statusCode = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = m_nonStreamBuffer + m_reply->readAll();
        if (statusCode > 0)
            err += QStringLiteral(" (HTTP %1)").arg(statusCode);
        if (!body.trimmed().isEmpty())
            err += QStringLiteral("\n") + QString::fromUtf8(body);
        cleanupReply();
        emit errorOccurred(withErrorTag(err));
        return;
    }

    // Non-streaming response
    if (!m_cfg.stream)
    {
        // Use buffered body (see onReadyRead)
        const QByteArray payload = m_nonStreamBuffer.isEmpty() ? m_reply->readAll() : m_nonStreamBuffer;
        QJsonParseError e;
        auto doc = QJsonDocument::fromJson(payload, &e);
        if (e.error != QJsonParseError::NoError)
        {
            cleanupReply();
            emit errorOccurred(withErrorTag(tr("响应 JSON 解析失败")));
            return;
        }
        auto o = doc.object();
        auto choices = o.value("choices").toArray();
        if (!choices.isEmpty())
        {
            auto msg = choices.first().toObject().value("message").toObject();
            m_accumulated = msg.value("content").toString();

            const QJsonArray toolCalls = msg.value("tool_calls").toArray();
            for (const QJsonValue& item : toolCalls)
            {
                const QJsonObject callObj = item.toObject();
                const QJsonObject fnObj = callObj.value("function").toObject();
                const QString toolName = fnObj.value("name").toString();
                const QString toolInput = fnObj.value("arguments").toString();
                const QString toolCallId = callObj.value("id").toString();
                if (!toolName.isEmpty() && !toolCallId.trimmed().isEmpty())
                {
                    emit toolCallRequested(toolName, toolInput, toolCallId);
                    break;
                }
            }
        }
    }

    const QString out = m_accumulated;
    cleanupReply();
    emit finished(out);
}

void OpenAIChatClient::consumeSseLines(const QByteArray& chunk)
{
    // SSE is event-based: events are separated by a blank line ("\n\n").
    // Each event can contain multiple lines like:
    //   data: {...}\n
    //   data: [DONE]\n\n
    // Parsing line-by-line can split JSON across reads and cause token duplication.
    m_streamBuffer += chunk;

    while (true)
    {
        const int sep = m_streamBuffer.indexOf("\n\n");
        if (sep < 0)
            break;

        const QByteArray event = m_streamBuffer.left(sep);
        m_streamBuffer.remove(0, sep + 2);

        // Collect all `data:` lines and join with \n (per SSE spec).
        QByteArray dataJoined;
        const QList<QByteArray> lines = event.split('\n');
        for (QByteArray line : lines)
        {
            if (!line.isEmpty() && line.endsWith('\r'))
                line.chop(1);
            const QByteArray prefix = "data:";
            if (!line.startsWith(prefix))
                continue;

            QByteArray d = line.mid(prefix.size()).trimmed();
            if (dataJoined.isEmpty())
                dataJoined = d;
            else
                dataJoined += "\n" + d;
        }

        if (dataJoined.isEmpty())
            continue;

        if (dataJoined == "[DONE]")
            continue;

        // For OpenAI-compatible streaming, each event is a standalone JSON object.
        QJsonParseError e;
        const auto doc = QJsonDocument::fromJson(dataJoined, &e);
        if (e.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const auto o = doc.object();
        const auto choices = o.value("choices").toArray();
        if (choices.isEmpty())
            continue;

        const QJsonObject choice = choices.first().toObject();
        const auto delta = choice.value("delta").toObject();

        const QJsonArray toolCalls = delta.value("tool_calls").toArray();
        for (const QJsonValue& item : toolCalls)
        {
            const QJsonObject callObj = item.toObject();
            const int idx = callObj.value("index").toInt(-1);
            if (idx < 0)
                continue;

            const QString callId = callObj.value("id").toString();
            if (!callId.isEmpty())
                m_streamToolIdByIndex[idx] = callId;

            const QJsonObject fnObj = callObj.value("function").toObject();
            const QString toolName = fnObj.value("name").toString();
            const QString toolArgs = fnObj.value("arguments").toString();
            if (!toolName.isEmpty())
                m_streamToolNameByIndex[idx] = toolName;
            if (!toolArgs.isEmpty())
                m_streamToolArgsByIndex[idx] += toolArgs;
        }

        const QString finishReason = choice.value("finish_reason").toString();
        if (finishReason == QStringLiteral("tool_calls"))
        {
            const QList<int> keys = m_streamToolNameByIndex.keys();
            for (const int idx : keys)
            {
                const QString name = m_streamToolNameByIndex.value(idx);
                const QString args = m_streamToolArgsByIndex.value(idx);
                const QString callId = m_streamToolIdByIndex.value(idx);
                if (!name.isEmpty() && !callId.trimmed().isEmpty())
                {
                    emit toolCallRequested(name, args, callId);
                    break;
                }
            }
            m_streamToolNameByIndex.clear();
            m_streamToolArgsByIndex.clear();
            m_streamToolIdByIndex.clear();
        }

        const QString t = delta.value("content").toString();
        if (t.isEmpty())
            continue;

        m_accumulated += t;
        emit tokenReceived(t);
    }
}

void OpenAIChatClient::handleSseLine(const QByteArray& line)
{
    // Legacy line-based SSE parsing is intentionally unused.
    Q_UNUSED(line);
}

void OpenAIChatClient::emitErrorAndCleanup(const QString& message)
{
    cleanupReply();
    emit errorOccurred(withErrorTag(message));
}

void OpenAIChatClient::cleanupReply()
{
    if (!m_reply) return;
    m_reply->disconnect(this);
    m_reply->deleteLater();
    m_reply = nullptr;
}
