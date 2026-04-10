#include "ai/ChatController.hpp"

#include "ai/OpenAIChatClient.hpp"
#include "ai/OpenAITtsClient.hpp"
#include "ai/AgentRuntime.hpp"
#include "ai/ConversationRepository.hpp"
#include "ai/core/IChatProvider.hpp"
#include "ai/core/ITtsProvider.hpp"
#include "ai/core/IMcpAdapter.hpp"
#include "ai/core/McpServerConfig.hpp"
#include "ai/core/ToolRegistry.hpp"

#include "common/SettingsManager.hpp"
#include "ui/ChatWindow.hpp"
#include "engine/Renderer.hpp"

#include <QtMultimedia/QAudioOutput>
#include <QtMultimedia/QMediaPlayer>
#include <QtMultimedia/QMediaDevices>
#include <QtMultimedia/QAudioDevice>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QByteArrayView>
#include <QProcessEnvironment>
#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QApplication>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
QJsonObject fallbackJsonSchema()
{
    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), QJsonObject{});
    return schema;
}

QString sanitizeToolToken(const QString& raw)
{
    QString out = raw.trimmed();
    out.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    if (out.isEmpty())
        out = QStringLiteral("tool");
    const QChar first = out.front();
    if (!(first.isLetter() || first == QLatin1Char('_')))
        out.prepend(QStringLiteral("t_"));
    return out;
}

QString buildSafeExposedToolName(const QString& serverName, const QString& toolName, QSet<QString>* usedNames)
{
    const QString server = sanitizeToolToken(serverName);
    const QString tool = sanitizeToolToken(toolName);

    QString name = QStringLiteral("mcp_%1__%2").arg(server, tool);
    if (name.size() > 64)
    {
        const uint h = qHash(serverName + QStringLiteral("::") + toolName);
        name = QStringLiteral("mcp_%1__%2_%3")
            .arg(server.left(16), tool.left(24), QString::number(h, 16));
    }

    if (usedNames)
    {
        QString unique = name;
        int idx = 1;
        while (usedNames->contains(unique))
        {
            unique = QStringLiteral("%1_%2").arg(name.left(60), QString::number(idx));
            ++idx;
        }
        usedNames->insert(unique);
        return unique;
    }

    return name;
}

bool tryResolveFromExposedNameHeuristic(const QString& exposedToolName,
                                        const QList<McpServerConfig>& serverConfigs,
                                        QString* serverName,
                                        QString* rawToolName)
{
    for (const McpServerConfig& cfg : serverConfigs)
    {
        const QString prefix = QStringLiteral("mcp_") + sanitizeToolToken(cfg.name) + QStringLiteral("__");
        if (!exposedToolName.startsWith(prefix))
            continue;

        const QString candidateTool = exposedToolName.mid(prefix.size()).trimmed();
        if (candidateTool.isEmpty())
            continue;

        if (serverName)
            *serverName = cfg.name;
        if (rawToolName)
            *rawToolName = candidateTool;
        return true;
    }

    return false;
}

QJsonObject toOpenAiFunctionTool(const QJsonObject& rawTool, const QString& exposedName)
{

    if (rawTool.value(QStringLiteral("function")).isObject())
    {
        QJsonObject fnObj = rawTool.value(QStringLiteral("function")).toObject();
        const QString rawName = fnObj.value(QStringLiteral("name")).toString().trimmed();
        if (rawName.isEmpty())
            return QJsonObject{};

        fnObj.insert(QStringLiteral("name"), exposedName);
        if (!fnObj.value(QStringLiteral("parameters")).isObject())
        {
            if (rawTool.value(QStringLiteral("inputSchema")).isObject())
                fnObj.insert(QStringLiteral("parameters"), rawTool.value(QStringLiteral("inputSchema")).toObject());
            else if (rawTool.value(QStringLiteral("input_schema")).isObject())
                fnObj.insert(QStringLiteral("parameters"), rawTool.value(QStringLiteral("input_schema")).toObject());
            else
                fnObj.insert(QStringLiteral("parameters"), fallbackJsonSchema());
        }

        QJsonObject toolObj;
        toolObj.insert(QStringLiteral("type"), QStringLiteral("function"));
        toolObj.insert(QStringLiteral("function"), fnObj);
        return toolObj;
    }

    const QString rawName = rawTool.value(QStringLiteral("name")).toString().trimmed();
    if (rawName.isEmpty())
        return QJsonObject{};

    QJsonObject fnObj;
    fnObj.insert(QStringLiteral("name"), exposedName);

    const QString desc = rawTool.value(QStringLiteral("description")).toString();
    if (!desc.isEmpty())
        fnObj.insert(QStringLiteral("description"), desc);

    if (rawTool.value(QStringLiteral("inputSchema")).isObject())
        fnObj.insert(QStringLiteral("parameters"), rawTool.value(QStringLiteral("inputSchema")).toObject());
    else if (rawTool.value(QStringLiteral("input_schema")).isObject())
        fnObj.insert(QStringLiteral("parameters"), rawTool.value(QStringLiteral("input_schema")).toObject());
    else if (rawTool.value(QStringLiteral("parameters")).isObject())
        fnObj.insert(QStringLiteral("parameters"), rawTool.value(QStringLiteral("parameters")).toObject());
    else
        fnObj.insert(QStringLiteral("parameters"), fallbackJsonSchema());

    QJsonObject toolObj;
    toolObj.insert(QStringLiteral("type"), QStringLiteral("function"));
    toolObj.insert(QStringLiteral("function"), fnObj);
    return toolObj;
}

QList<McpServerStatus> buildMcpStatuses(const QList<McpServerConfig>& serverConfigs,
                                        McpServerRuntimeState enabledState,
                                        const QHash<QString, QString>& details = {})
{
    QList<McpServerStatus> statuses;
    statuses.reserve(serverConfigs.size());
    for (const McpServerConfig& serverConfig : serverConfigs)
    {
        McpServerStatus status;
        status.name = serverConfig.name;
        status.enabled = serverConfig.enabled;
        status.state = serverConfig.enabled ? enabledState : McpServerRuntimeState::Disabled;
        status.detail = details.value(serverConfig.name);
        statuses.push_back(status);
    }
    return statuses;
}

bool sameMcpServerConfig(const McpServerConfig& lhs, const McpServerConfig& rhs)
{
    return lhs.name == rhs.name
        && lhs.type == rhs.type
        && lhs.enabled == rhs.enabled
        && lhs.timeoutMs == rhs.timeoutMs
        && lhs.command == rhs.command
        && lhs.args == rhs.args
        && lhs.env == rhs.env
        && lhs.url == rhs.url
        && lhs.headers == rhs.headers;
}

QList<McpServerStatus> buildMcpStatusesFromCache(const QList<McpServerConfig>& serverConfigs,
                                                 const QHash<QString, McpServerStatus>& statusCache)
{
    QList<McpServerStatus> statuses;
    statuses.reserve(serverConfigs.size());

    for (const McpServerConfig& serverConfig : serverConfigs)
    {
        McpServerStatus status;
        status.name = serverConfig.name;
        status.enabled = serverConfig.enabled;

        if (!serverConfig.enabled)
        {
            status.state = McpServerRuntimeState::Disabled;
        }
        else if (statusCache.contains(serverConfig.name))
        {
            status = statusCache.value(serverConfig.name);
            status.name = serverConfig.name;
            status.enabled = true;
        }
        else
        {
            status.state = McpServerRuntimeState::Starting;
        }

        statuses.push_back(status);
    }

    return statuses;
}

void composeMergedMcpTools(const QList<McpServerConfig>& serverConfigs,
                          const QHash<QString, QJsonArray>& rawToolsCache,
                          QJsonArray* mergedTools,
                          QHash<QString, QPair<QString, QString>>* toolRoutes)
{
    if (!mergedTools || !toolRoutes)
        return;

    *mergedTools = QJsonArray{};
    toolRoutes->clear();

    QSet<QString> usedExposedNames;
    for (const McpServerConfig& serverConfig : serverConfigs)
    {
        if (!serverConfig.enabled)
            continue;

        const auto cachedToolsIt = rawToolsCache.constFind(serverConfig.name);
        if (cachedToolsIt == rawToolsCache.constEnd())
            continue;

        for (const QJsonValue& toolValue : cachedToolsIt.value())
        {
            if (!toolValue.isObject())
                continue;

            const QJsonObject rawToolObj = toolValue.toObject();
            QString rawToolName;
            if (rawToolObj.value(QStringLiteral("function")).isObject())
                rawToolName = rawToolObj.value(QStringLiteral("function")).toObject().value(QStringLiteral("name")).toString().trimmed();
            else
                rawToolName = rawToolObj.value(QStringLiteral("name")).toString().trimmed();

            if (rawToolName.isEmpty())
                continue;

            const QString exposedName = buildSafeExposedToolName(serverConfig.name, rawToolName, &usedExposedNames);
            const QJsonObject toolObj = toOpenAiFunctionTool(rawToolObj, exposedName);
            if (toolObj.isEmpty())
                continue;

            mergedTools->append(toolObj);
            toolRoutes->insert(exposedName, qMakePair(serverConfig.name, rawToolName));
        }
    }
}
} // namespace

ChatController::ChatController(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<McpServerStatus>("McpServerStatus");
    qRegisterMetaType<QList<McpServerStatus>>("QList<McpServerStatus>");

    m_toolRegistry = std::make_unique<ToolRegistry>();
    m_lipDebug = (QProcessEnvironment::systemEnvironment().value(QStringLiteral("AMAI_LIPSYNC_DEBUG")) == QStringLiteral("1"));
    m_useAgentRuntime = true;

    auto* client = new OpenAIChatClient(this);
    m_client = client;
    auto* tts = new OpenAITtsClient(this);
    m_tts = tts;
    reloadMcpTools();
    if (m_useAgentRuntime)
    {
        m_runtime = new AgentRuntime(this);
        m_runtime->setChatProvider(m_client);
        m_runtime->setConversationStore(nullptr);
        m_runtime->setTtsProvider(m_tts);

        connect(m_runtime, &AgentRuntime::requestAppendUserMessage, this, [this](const QString& text){
            if (m_chatWindow) m_chatWindow->appendUserMessage(text);
        });
        connect(m_runtime, &AgentRuntime::requestAppendAiMessageStart, this, [this]{
            if (m_chatWindow) m_chatWindow->appendAiMessageStart();
        });
        connect(m_runtime, &AgentRuntime::requestSetBusy, this, [this](bool busy){
            m_runtimeBusyRequested = busy;
            syncChatBusyUi();
        });
        connect(m_runtime, &AgentRuntime::requestConversationCleared, this, [this](const QString& modelFolder){
            if (m_chatWindow) m_chatWindow->loadFromDisk(modelFolder);
        });
        connect(m_runtime, &AgentRuntime::requestStartTts, this, &ChatController::onRuntimeRequestStartTts);
        connect(m_runtime, &AgentRuntime::requestStartPlayback, this, &ChatController::onRuntimeRequestStartPlayback);
        connect(m_runtime, &AgentRuntime::requestStartPacedTextReveal, this, &ChatController::onRuntimeRequestStartPacedTextReveal);
        connect(m_runtime, &AgentRuntime::toolCallRequested, this, &ChatController::onRuntimeToolCallRequested);
        connect(m_runtime, &AgentRuntime::stateChanged, this, [](IAgentRuntime::State st){
            qDebug().noquote() << "[AgentRuntime] state=" << int(st);
        });
    }

    if (m_useAgentRuntime && m_runtime)
    {
        connect(m_runtime, &AgentRuntime::tokenReceived, this, &ChatController::onClientToken);
        connect(m_runtime, &AgentRuntime::finished, this, &ChatController::onClientFinished);
        connect(m_runtime, &AgentRuntime::errorOccurred, this, &ChatController::onClientError);
    }
    else
    {
        connect(client, &OpenAIChatClient::tokenReceived, this, &ChatController::onClientToken);
        connect(client, &OpenAIChatClient::finished, this, &ChatController::onClientFinished);
        connect(client, &OpenAIChatClient::errorOccurred, this, &ChatController::onClientError);
    }

    if (!(m_useAgentRuntime && m_runtime))
    {
        connect(tts, &OpenAITtsClient::finished, this, &ChatController::onTtsFinished);
        connect(tts, &OpenAITtsClient::errorOccurred, this, &ChatController::onTtsError);
    }

    m_audioOut = new QAudioOutput(this);
    m_player = new QMediaPlayer(this);
    m_player->setAudioOutput(m_audioOut);

    applyPreferredAudioOutput();

    m_lipTimer.setInterval(33);
    m_lipTimer.setSingleShot(false);
    connect(&m_lipTimer, &QTimer::timeout, this, &ChatController::onLipTimer);

    // stop lip sync when playback ends
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState st){
        // IMPORTANT: setSource() may transiently switch to Stopped while we are preparing playback.
        if (m_preparingPlayback) return;

        if (st != QMediaPlayer::PlayingState)
        {
            stopWavLipSyncClock();
            m_lipTimer.stop();
            m_lipFrames.clear();
            m_lipFrameIndex = 0;
            m_lipSyncActiveForPlayback = false;
            releasePlaybackSource();
            if (m_renderer) {
                m_renderer->setLipSyncActive(false);
                m_renderer->setLipSyncValue(0.0f);
            }
            m_wavLip.reset();

            // Ensure UI lock state follows runtime + output presentation.
            syncChatBusyUi();
        }
    });

    // paced reveal timer (text sync with TTS)
    m_pacedRevealTimer.setInterval(33);
    m_pacedRevealTimer.setSingleShot(false);
    connect(&m_pacedRevealTimer, &QTimer::timeout, this, [this] {
        if (!m_chatWindow || !m_player)
            return;

        const qint64 dur = m_player->duration();
        const qint64 pos = m_player->position();
        if (m_player->playbackState() != QMediaPlayer::PlayingState || dur <= 0)
            return;

        const qint64 remainMs = std::max<qint64>(1, dur - pos);
        const int fullLen = m_pacedFullText.size();
        const int remainChars = std::max(0, fullLen - m_pacedRevealChars);

        const double charsPerMs = double(remainChars) / double(remainMs);
        int step = int(std::ceil(charsPerMs * 33.0));
        if (step < 1) step = 1;
        if (step > 256) step = 256;

        m_pacedRevealChars = std::min(fullLen, m_pacedRevealChars + step);
        const QString partial = m_pacedFullText.left(m_pacedRevealChars);

        // Ensure there is exactly one assistant draft bubble, then only update its content.
        if (!m_pacedBubbleStarted)
        {
            m_chatWindow->appendAiMessageStart();
            m_pacedBubbleStarted = true;
        }
        m_chatWindow->setAiMessageContent(partial);

        if (m_pacedRevealChars >= fullLen)
        {
            m_pacedRevealTimer.stop();
            m_chatWindow->sealCurrentAiBubble();
        }
    });
}

void ChatController::setChatWindow(ChatWindow* wnd)
{
    if (m_chatWindow == wnd)
        return;

    if (m_chatWindow)
    {
        disconnect(m_chatWindow, nullptr, this, nullptr);
    }

    m_chatWindow = wnd;

    if (!m_chatWindow)
        return;

    // In runtime mode, AgentRuntime is the single source of truth for chat persistence.
    // ChatWindow should only render UI and never write chat json directly.
    m_chatWindow->setPersistenceEnabled(!(m_useAgentRuntime && m_runtime));

    connect(m_chatWindow, &ChatWindow::requestSendMessage, this, &ChatController::onSendRequested);
    connect(m_chatWindow, &ChatWindow::requestAbortCurrentTask, this, &ChatController::onAbortRequested);
    connect(m_chatWindow, &ChatWindow::requestClearChat, this, &ChatController::onClearRequested);
    connect(m_chatWindow, &ChatWindow::requestRefreshMcpServerStatuses, this, &ChatController::refreshMcpServerStatuses);
    connect(m_chatWindow, &ChatWindow::requestSetMcpServerEnabled, this, &ChatController::setMcpServerEnabled);
    connect(this, &ChatController::mcpServerStatusesChanged, m_chatWindow, &ChatWindow::setMcpServerStatuses);

    // sync current model context to window
    if (!m_modelFolder.isEmpty())
        m_chatWindow->setCurrentModel(m_modelFolder, m_modelDir);

    m_chatWindow->setMcpServerStatuses(m_mcpStatuses);
}

void ChatController::setRenderer(Renderer* renderer)
{
    m_renderer = renderer;
}

void ChatController::refreshTtsConfig()
{
    ITtsProvider::TtsConfig cfg;
    cfg.baseUrl = SettingsManager::instance().ttsBaseUrl();
    cfg.apiKey = SettingsManager::instance().ttsApiKey();
    cfg.model = SettingsManager::instance().ttsModel();
    cfg.voice = SettingsManager::instance().ttsVoice();
    m_tts->setConfig(cfg);
}

QString ChatController::cacheAudioPathForModel(const QString& modelFolder) const
{
    const QString base = SettingsManager::instance().cacheDir();
    // overwrite per model
    const QString safe = modelFolder.isEmpty() ? QStringLiteral("default") : modelFolder;
    return QDir(base).filePath(safe + QStringLiteral(".tts.wav"));
}

void ChatController::stopPlayback()
{
    m_preparingPlayback = false;
    stopWavLipSyncClock();

    if (m_pacedRevealTimer.isActive())
        m_pacedRevealTimer.stop();
    m_pacedFullText.clear();
    m_pacedRevealChars = 0;

    m_lipTimer.stop();
    m_lipFrames.clear();
    m_lipFrameIndex = 0;
    m_lipSyncActiveForPlayback = false;
    releasePlaybackSource();
    if (m_renderer) {
        m_renderer->setLipSyncActive(false);
        m_renderer->setLipSyncValue(0.0f);
    }
    m_wavLip.reset();
}

void ChatController::releasePlaybackSource()
{
    if (!m_player)
        return;

    if (m_player->playbackState() != QMediaPlayer::StoppedState)
        m_player->stop();

    if (!m_player->source().isEmpty())
        m_player->setSource(QUrl());
}

void ChatController::startWavLipSyncClock()
{
    m_wavClock.restart();
    m_wavClockStarted = true;
    // reset wav handler offsets
    m_wavLip.lastOffset = 0;
    m_wavLip.currentRms = 0.0f;
}

void ChatController::stopWavLipSyncClock()
{
    m_wavClockStarted = false;
}

float ChatController::updateLipRmsFromClock()
{
    if (!m_wavLip.loaded || !m_wavClockStarted)
        return 0.0f;

    if (m_wavLip.sampleRate <= 0 || m_wavLip.channels <= 0)
        return 0.0f;

    // python: currentTime = time.time() - startTime; currentOffset = int(currentTime * sampleRate)
    const double t = double(m_wavClock.elapsed()) / 1000.0;
    qsizetype currentOffset = qsizetype(std::floor(t * double(m_wavLip.sampleRate)));

    if (currentOffset < 0) currentOffset = 0;
    if (currentOffset > m_wavLip.totalFrames) currentOffset = m_wavLip.totalFrames;

    // end
    if (m_wavLip.lastOffset >= m_wavLip.totalFrames)
        return 0.0f;

    // time too short
    if (currentOffset == m_wavLip.lastOffset)
        return m_wavLip.currentRms;

    const qsizetype start = m_wavLip.lastOffset;
    const qsizetype end = currentOffset;

    const uchar* pcm = reinterpret_cast<const uchar*>(m_wavLip.pcm.constData());
    const qsizetype pcmBytes = m_wavLip.pcm.size();

    const int channels = m_wavLip.channels;
    const int bits = m_wavLip.bitsPerSample;
    const int audioFormat = m_wavLip.audioFormat;

    const int bytesPerSample = bits / 8;
    const int bytesPerFrame = bytesPerSample * channels;
    if (bytesPerFrame <= 0) return 0.0f;

    auto sampleAt = [&](qsizetype frameIndex, int ch) -> float {
        const qsizetype idx = frameIndex * channels + ch;
        if (audioFormat == 1 && bits == 8)
        {
            const qsizetype byteOff = idx;
            if (byteOff >= pcmBytes) return 0.0f;
            const int v = int(pcm[byteOff]) - 128;
            return float(double(v) / 128.0);
        }
        if (audioFormat == 1 && bits == 16)
        {
            const qsizetype byteOff = idx * 2;
            if (byteOff + 1 >= pcmBytes) return 0.0f;
            const qint16 v = qint16(pcm[byteOff] | (pcm[byteOff + 1] << 8));
            return float(double(v) / 32768.0);
        }
        if (audioFormat == 1 && bits == 32)
        {
            const qsizetype byteOff = idx * 4;
            if (byteOff + 3 >= pcmBytes) return 0.0f;
            const qint32 v = qint32(quint32(pcm[byteOff]) | (quint32(pcm[byteOff + 1]) << 8) | (quint32(pcm[byteOff + 2]) << 16) | (quint32(pcm[byteOff + 3]) << 24));
            return float(double(v) / 2147483648.0);
        }
        if (audioFormat == 3 && bits == 32)
        {
            const qsizetype byteOff = idx * 4;
            if (byteOff + 3 >= pcmBytes) return 0.0f;
            float v;
            std::memcpy(&v, pcm + byteOff, 4);
            return v;
        }
        return 0.0f;
    };

    double sum2 = 0.0;
    qsizetype n = 0;

    const qsizetype frames = end - start;
    const qsizetype stride = (frames > 2048) ? 2 : 1;

    for (qsizetype i = start; i < end; i += stride)
    {
        double acc = 0.0;
        for (int c = 0; c < channels; ++c)
            acc += double(sampleAt(i, c));
        const double mono = acc / double(channels);
        sum2 += mono * mono;
        ++n;
    }

    const double mean2 = (n > 0) ? (sum2 / double(n)) : 0.0;
    const float rms = float(std::sqrt(mean2));

    m_wavLip.currentRms = rms;
    m_wavLip.lastOffset = end;

    return rms;
}

void ChatController::maybeDebugLip(const char* tag, float rms, float outV)
{
    if (!m_lipDebug) return;
    qDebug().noquote() << "[LipSync]" << tag
                       << "state=" << (m_player ? int(m_player->playbackState()) : -1)
                       << "elapsedMs=" << (m_wavClockStarted ? qint64(m_wavClock.elapsed()) : -1)
                       << "lastOff=" << m_wavLip.lastOffset
                       << "total=" << m_wavLip.totalFrames
                       << "rms=" << rms
                       << "out=" << outV;
}

void ChatController::startPlaybackWithLip(const QString& audioPath)
{
    if (!m_player || !m_audioOut) return;

    m_preparingPlayback = true;

    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        m_player->stop();

    m_lipTimer.stop();
    m_lipFrameIndex = 0;

    m_player->setSource(QUrl());
    m_player->setSource(QUrl::fromLocalFile(audioPath));
    m_audioOut->setVolume(1.0);

    // Load wav PCM for lip sync driven by our own clock.
    const bool loaded = loadWavForLipSync(audioPath);
    if (loaded)
    {
        startWavLipSyncClock();
    }

    // Enable lip-sync for this playback.
    m_lipSyncActiveForPlayback = true;
    if (m_renderer) m_renderer->setLipSyncActive(true);

    m_player->play();

    m_preparingPlayback = false;

    m_lipTimer.start();
}

void ChatController::onLipTimer()
{
    if (!m_renderer) return;
    if (!m_player) return;

    if (!m_lipSyncActiveForPlayback)
        return;

    if (m_player->playbackState() != QMediaPlayer::PlayingState)
    {
        m_lipSyncActiveForPlayback = false;
        m_renderer->setLipSyncValue(0.0f);
        m_renderer->setLipSyncActive(false);
        m_lipTimer.stop();
        m_wavLip.reset();
        stopWavLipSyncClock();
        return;
    }

    float v = 0.0f;
    float rms = 0.0f;

    // Primary: RMS computed from WAV using elapsed clock (WavHandler-like).
    if (m_wavLip.loaded)
    {
        rms = updateLipRmsFromClock();

        // normalize with a fixed-ish reference and user gain
        const float ref = 0.18f; // good for normalized PCM; conservative
        v = (ref > 1e-5f) ? (rms / ref) : 0.0f;
        v = std::clamp(v * 2.6f * m_lipGain, 0.0f, 1.0f);
    }
    else if (!m_lipFrames.isEmpty())
    {
        // Fallback: precomputed frames
        const int idx = m_lipFrameIndex++;
        v = (idx >= m_lipFrames.size()) ? m_lipFrames.back() : m_lipFrames[idx];
        if (v > 0.0001f) v = std::clamp(v + 0.02f, 0.0f, 1.0f);
    }

    maybeDebugLip("tick", rms, v);
    m_renderer->setLipSyncValue(v);
}

// ---- missing slots / helpers (were declared in header, referenced by moc) ----

void ChatController::onClientToken(const QString& token)
{
    if (!m_chatWindow) return;
    if (!m_streamEnabledForCurrentReply) return;

    // Runtime handles TTS-paced reveal by itself; skip immediate token painting
    // to avoid duplicated "stream once, then stream again with audio" effect.
    if (m_useAgentRuntime && m_runtime && m_ttsEnabledForCurrentReply)
        return;

    // If TTS is enabled, we pace text streaming with audio playback (see onTtsFinished)
    // to avoid text finishing before speech starts.
    if (m_ttsEnabledForCurrentReply)
    {
        // Collect tokens; we'll reveal them later when audio starts.
        m_pendingStreamText += token;
        return;
    }

    if (!token.isEmpty())
        m_chatWindow->appendAiToken(token);
}

void ChatController::onClientFinished(const QString& fullText)
{
    if (!m_chatWindow)
        return;

    if (m_useAgentRuntime && m_runtime)
    {
        const bool streamEnabled = m_streamEnabledForCurrentReply;
        const bool ttsEnabled = m_ttsEnabledForCurrentReply;

        if (streamEnabled && !ttsEnabled)
        {
            // Token stream has already filled the current draft bubble.
            m_chatWindow->sealCurrentAiBubble();
        }
        else if (streamEnabled && ttsEnabled)
        {
            // paced reveal path: do not finalize here; timer handles bubble content and sealing.
            if (!m_pacedRevealTimer.isActive())
                m_chatWindow->sealCurrentAiBubble();
        }
        else if (!fullText.trimmed().isEmpty())
        {
            // non-stream final response segment
            m_chatWindow->finalizeAssistantMessage(fullText, /*ensureBubbleExists*/true);
        }

        // Keep UI and input lock state aligned with runtime state machine.
        m_pendingFinalText.clear();
        m_runtimeBusyRequested = m_runtime->isBusy();
        syncChatBusyUi();
        return;
    }

    // If TTS is enabled, defer finalizing/persisting the assistant message until
    // we are ready to start playback. This avoids writing the same assistant
    // message twice (onClientFinished + onTtsFinished).
    m_pendingFinalText = fullText;

    if (m_ttsEnabledForCurrentReply)
    {
        const QString outPath = cacheAudioPathForModel(m_modelFolder);
        m_tts->startSpeech(fullText, outPath);
        // Keep busy until audio playback ends (prevents overlap).
        return;
    }

    // No TTS => finalize immediately (single source of truth for persistence).
    m_chatWindow->finalizeAssistantMessage(fullText, /*ensureBubbleExists*/true);
    m_pendingFinalText.clear();
    m_chatWindow->setBusy(false);
}

void ChatController::onTtsFinished(const QString& outPath)
{
    if (!m_chatWindow) return;

    // Start playback first.
    if (!outPath.isEmpty())
        startPlaybackWithLip(outPath);

    // If streaming is enabled and we have streamed tokens, reveal them paced to audio.
    const bool wantStream = SettingsManager::instance().aiStreamEnabled();
    if (wantStream && !m_pendingStreamText.isEmpty())
    {
        startPacedTextReveal(m_pendingStreamText);
        m_pendingStreamText.clear();
        // Finalization + persistence will happen when paced reveal finishes.
        return;
    }

    // Non-stream (or no tokens captured): finalize immediately.
    if (!m_pendingFinalText.isEmpty())
    {
        m_chatWindow->finalizeAssistantMessage(m_pendingFinalText, /*ensureBubbleExists*/true);
        m_pendingFinalText.clear();
    }


    // busy will be cleared when playback stops.
}

void ChatController::startPacedTextReveal(const QString& fullText)
{
    if (!m_chatWindow)
        return;

    m_pacedFullText = fullText;
    m_pacedRevealChars = 0;
    m_pacedBubbleStarted = false;

    if (!m_pacedRevealTimer.isActive())
        m_pacedRevealTimer.start();
}

void ChatController::onSendRequested(const QString& modelFolder, const QString& userText)
{
    if (!m_chatWindow) return;
    if (modelFolder.isEmpty() || userText.trimmed().isEmpty()) return;

    m_modelFolder = modelFolder;
    if (m_mcpToolsDirty || m_mcpToolsReloadInProgress)
    {
        m_pendingSendModelFolder = modelFolder;
        m_pendingSendText = userText;
        m_hasPendingSend = true;

        if (m_mcpToolsDirty)
            reloadMcpTools();
        return;
    }

    dispatchSendNow(modelFolder, userText);
}

void ChatController::onAbortRequested()
{
    if (!m_chatWindow)
        return;

    m_pendingSendModelFolder.clear();
    m_pendingSendText.clear();
    m_hasPendingSend = false;

    stopPlayback();
    if (m_pacedRevealTimer.isActive())
        m_pacedRevealTimer.stop();
    m_pacedFullText.clear();
    m_pacedRevealChars = 0;

    if (m_client)
        m_client->cancel();
    if (m_tts)
        m_tts->cancel();
    if (m_useAgentRuntime && m_runtime)
        m_runtime->cancel();

    m_pendingFinalText.clear();
    m_pendingStreamText.clear();
    m_runtimeBusyRequested = false;

    if (m_chatWindow)
    {
        if (m_chatWindow->currentAssistantDraft().trimmed().isEmpty())
            m_chatWindow->cancelCurrentAiDraftBubble();
        else
            m_chatWindow->sealCurrentAiBubble();
    }

    syncChatBusyUi();
}

void ChatController::dispatchSendNow(const QString& modelFolder, const QString& userText)
{
    if (!m_chatWindow) return;
    if (modelFolder.isEmpty() || userText.trimmed().isEmpty()) return;

    if (m_useAgentRuntime && m_runtime)
    {
        m_runtime->setModelContext(modelFolder, m_modelDir);

        m_streamEnabledForCurrentReply = SettingsManager::instance().aiStreamEnabled();
        m_ttsEnabledForCurrentReply = !SettingsManager::instance().ttsBaseUrl().trimmed().isEmpty();

        if (m_runtime->isBusy())
        {
            m_chatWindow->setBusy(false);
            return;
        }

        stopPlayback();
        m_runtime->submitUserMessage(userText);
        return;
    }

    if ((m_client && m_client->isBusy()) || m_tts->isBusy() || (m_player && m_player->playbackState() == QMediaPlayer::PlayingState))
        return;

    stopPlayback();

    m_streamEnabledForCurrentReply = SettingsManager::instance().aiStreamEnabled();
    refreshTtsConfig();
    m_ttsEnabledForCurrentReply = !m_tts->config().baseUrl.trimmed().isEmpty();
    m_pendingStreamText.clear();
    m_pendingStreamText.reserve(int(std::max<qsizetype>(256, userText.size() * 4)));

    // Stable path: controller directly drives request lifecycle.
    m_chatWindow->setBusy(true);
    refreshClientConfig();
    m_chatWindow->appendUserMessage(userText);
    m_chatWindow->appendAiMessageStart();
    const QByteArray msgJson = QJsonDocument(ConversationRepository::loadMessages(modelFolder)).toJson(QJsonDocument::Compact);
    m_client->startChat(msgJson);
}

void ChatController::onClearRequested(const QString& modelFolder)
{
    if (!m_chatWindow) return;
    if (modelFolder.isEmpty()) return;

    if (m_toolRegistry)
        m_toolRegistry->clear();
    m_pendingSendModelFolder.clear();
    m_pendingSendText.clear();
    m_hasPendingSend = false;

    stopPlayback();
    if (m_pacedRevealTimer.isActive()) m_pacedRevealTimer.stop();
    m_pacedFullText.clear();
    m_pacedRevealChars = 0;

    m_client->cancel();
    m_tts->cancel();
    if (m_useAgentRuntime && m_runtime) m_runtime->cancel();
    m_pendingFinalText.clear();
    m_pendingStreamText.clear();
    m_runtimeBusyRequested = false;

    if (m_useAgentRuntime && m_runtime)
        m_runtime->clearConversation(modelFolder);
    else
    {
        saveClearedChat(modelFolder);
        m_chatWindow->loadFromDisk(modelFolder);
    }

    syncChatBusyUi();
}

void ChatController::refreshClientConfig()
{
    IChatProvider::ChatConfig cfg;
    cfg.baseUrl = SettingsManager::instance().aiBaseUrl();
    cfg.apiKey = SettingsManager::instance().aiApiKey();
    cfg.model = SettingsManager::instance().aiModel();
    cfg.systemPrompt = SettingsManager::instance().aiSystemPrompt();
    cfg.stream = SettingsManager::instance().aiStreamEnabled();

    if (!m_modelFolder.isEmpty())
        cfg.systemPrompt.replace(QStringLiteral("$name$"), m_modelFolder);

    m_client->setConfig(cfg);
}

// NOTE: buildMessagesPayload() is no longer used. OpenAIChatClient consumes the simplified
// messages array JSON directly, so we omit the wrapper payload builder.

void ChatController::saveClearedChat(const QString& modelFolder) const
{
    ConversationRepository::saveMessages(modelFolder, QJsonArray{});
}

bool ChatController::loadWavForLipSync(const QString& wavPath)
{
    m_wavLip.reset();

    QFile f(wavPath);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QByteArray data = f.readAll();
    if (data.size() < 44)
        return false;

    auto u16 = [&](int off) -> quint16 {
        const uchar* p = reinterpret_cast<const uchar*>(data.constData() + off);
        return quint16(p[0]) | (quint16(p[1]) << 8);
    };
    auto u32 = [&](int off) -> quint32 {
        const uchar* p = reinterpret_cast<const uchar*>(data.constData() + off);
        return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
    };

    if (QByteArrayView(data.constData(), 4) != "RIFF" || QByteArrayView(data.constData() + 8, 4) != "WAVE")
        return false;

    int pos = 12;
    int fmtPos = -1;
    int fmtSize = 0;
    int dataPos = -1;
    int dataSize = 0;

    while (pos + 8 <= data.size())
    {
        const QByteArrayView tag(data.constData() + pos, 4);
        const int sz = int(u32(pos + 4));
        const int chunkStart = pos + 8;

        if (tag == "fmt ")
        {
            fmtPos = chunkStart;
            fmtSize = sz;
        }
        else if (tag == "data")
        {
            dataPos = chunkStart;
            dataSize = sz;
            break;
        }

        pos = chunkStart + sz + (sz % 2);
    }

    if (fmtPos < 0 || dataPos < 0 || fmtSize < 16 || dataPos + dataSize > data.size())
        return false;

    m_wavLip.audioFormat = int(u16(fmtPos + 0));
    m_wavLip.channels = int(u16(fmtPos + 2));
    m_wavLip.sampleRate = int(u32(fmtPos + 4));
    m_wavLip.bitsPerSample = int(u16(fmtPos + 14));

    if (m_wavLip.channels <= 0 || m_wavLip.sampleRate <= 0)
        return false;

    const int bytesPerSample = m_wavLip.bitsPerSample / 8;
    const int bytesPerFrame = bytesPerSample * m_wavLip.channels;
    if (bytesPerFrame <= 0)
        return false;

    // Copy only PCM payload for fast access.
    m_wavLip.pcm = data.mid(dataPos, dataSize);
    const qsizetype pcmBytes = m_wavLip.pcm.size();
    m_wavLip.totalFrames = (bytesPerFrame > 0) ? (pcmBytes / bytesPerFrame) : 0;

    if (m_wavLip.totalFrames <= 0)
        return false;

    m_wavLip.lastOffset = 0;
    m_wavLip.currentRms = 0.0f;
    m_wavLip.loaded = true;
    return true;
}

QVector<float> ChatController::buildLipFramesFromWavFile(const QString& wavPath) const
{
    QFile f(wavPath);
    if (!f.open(QIODevice::ReadOnly))
    {
        return {};
    }

    const QByteArray data = f.readAll();
    if (data.size() < 44)
    {
        return {};
    }

    auto u16 = [&](int off) -> quint16 {
        const uchar* p = reinterpret_cast<const uchar*>(data.constData() + off);
        return quint16(p[0]) | (quint16(p[1]) << 8);
    };
    auto u32 = [&](int off) -> quint32 {
        const uchar* p = reinterpret_cast<const uchar*>(data.constData() + off);
        return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
    };

    if (QByteArrayView(data.constData(), 4) != "RIFF" || QByteArrayView(data.constData() + 8, 4) != "WAVE")
    {
        return {};
    }

    int pos = 12;
    int fmtPos = -1;
    int fmtSize = 0;
    int dataPos = -1;
    int dataSize = 0;
    while (pos + 8 <= data.size())
    {
        const QByteArrayView tag(data.constData() + pos, 4);
        const int sz = int(u32(pos + 4));
        const int chunkStart = pos + 8;
        if (tag == "fmt ")
        {
            fmtPos = chunkStart;
            fmtSize = sz;
        }
        else if (tag == "data")
        {
            dataPos = chunkStart;
            dataSize = sz;
            break;
        }
        pos = chunkStart + sz + (sz % 2);
    }

    if (fmtPos < 0 || dataPos < 0 || fmtSize < 16 || dataPos + dataSize > data.size())
    {
        return {};
    }

    const int audioFormat = int(u16(fmtPos + 0)); // 1=PCM, 3=float
    const int channels = int(u16(fmtPos + 2));
    const int sampleRate = int(u32(fmtPos + 4));
    const int bitsPerSample = int(u16(fmtPos + 14));

    if (channels <= 0 || sampleRate <= 0)
    {
        return {};
    }

    const uchar* pcm = reinterpret_cast<const uchar*>(data.constData() + dataPos);
    const int pcmBytes = dataSize;

    auto nextSampleMono = [&](int frameIndex) -> float {
        // Returns normalized mono sample [-1,1], averaging channels.
        double acc = 0.0;
        for (int c = 0; c < channels; ++c)
        {
            const int idx = frameIndex * channels + c;

            if (audioFormat == 1 && bitsPerSample == 8)
            {
                const int byteOff = idx;
                if (byteOff >= pcmBytes) break;
                // 8-bit PCM is unsigned [0..255] with 128 as zero.
                const int v = int(pcm[byteOff]) - 128;
                acc += double(v) / 128.0;
            }
            else if (audioFormat == 1 && bitsPerSample == 16)
            {
                const int byteOff = idx * 2;
                if (byteOff + 1 >= pcmBytes) break;
                const qint16 v = qint16(pcm[byteOff] | (pcm[byteOff + 1] << 8));
                acc += double(v) / 32768.0;
            }
            else if (audioFormat == 1 && bitsPerSample == 32)
            {
                const int byteOff = idx * 4;
                if (byteOff + 3 >= pcmBytes) break;
                const qint32 v = qint32(quint32(pcm[byteOff]) | (quint32(pcm[byteOff + 1]) << 8) | (quint32(pcm[byteOff + 2]) << 16) | (quint32(pcm[byteOff + 3]) << 24));
                acc += double(v) / 2147483648.0;
            }
            else if (audioFormat == 3 && bitsPerSample == 32)
            {
                const int byteOff = idx * 4;
                if (byteOff + 3 >= pcmBytes) break;
                float v;
                std::memcpy(&v, pcm + byteOff, 4);
                acc += double(v);
            }
            else
            {
                return 0.0f;
            }
        }
        return float(acc / double(channels));
    };

    const int bytesPerFrame = (bitsPerSample / 8) * channels;
    if (bytesPerFrame <= 0)
    {
        return {};
    }
    const int totalFrames = pcmBytes / bytesPerFrame;
    if (totalFrames <= 0)
    {
        return {};
    }

    constexpr int frameMs = 33;
    const int hop = std::max(1, int(std::round(double(sampleRate) * (double(frameMs) / 1000.0))));

    QVector<float> rms;
    rms.reserve(std::max(1, totalFrames / hop));

    for (int start = 0; start < totalFrames; start += hop)
    {
        const int end = std::min(totalFrames, start + hop);
        double sum2 = 0.0;
        int n = 0;
        for (int i = start; i < end; ++i)
        {
            const double s = nextSampleMono(i);
            sum2 += s * s;
            ++n;
        }
        const double mean2 = (n > 0) ? (sum2 / double(n)) : 0.0;
        rms.push_back(float(std::sqrt(mean2)));
    }

    if (rms.isEmpty())
    {
        return {};
    }

    QVector<float> sorted = rms;
    std::sort(sorted.begin(), sorted.end());

    const qsizetype p95 = std::clamp(
        qsizetype(sorted.size() * 0.95),
        qsizetype(0),
        qsizetype(sorted.size() - 1)
    );
    const float ref = std::max(1e-4f, sorted[int(p95)]);

    QVector<float> out;
    out.reserve(rms.size());

    // Slightly conservative gain so we don't look like yelling for loud recordings.
    const float lipSyncN = 2.4f;
    for (float v : rms)
    {
        float x = (v / ref) * lipSyncN;
        x = std::clamp(x, 0.0f, 1.0f);
        out.push_back(x);
    }

    return out;
}

QVector<float> ChatController::makeFallbackLipFrames() const
{
    QVector<float> v;
    v.reserve(300);
    for (int i = 0; i < 300; ++i)
    {
        const float t = float(i) * 0.033f;
        float x = 0.35f + 0.15f * std::sin(t * 18.0f) + 0.08f * std::sin(t * 7.5f + 1.3f);
        v.push_back(std::clamp(x, 0.0f, 1.0f));
    }
    return v;
}

void ChatController::onClientError(const QString& message)
{
    if (m_chatWindow)
    {
        // End the assistant bubble and unlock UI.
        m_chatWindow->finalizeAssistantMessage(message.isEmpty() ? tr("请求失败") : message, true);
        m_runtimeBusyRequested = false;
        syncChatBusyUi();
    }
}

void ChatController::onTtsError(const QString& message)
{
    if (!m_chatWindow) return;

    // Fall back: show text immediately even if TTS fails.
    if (!m_pendingFinalText.isEmpty())
    {
        m_chatWindow->finalizeAssistantMessage(m_pendingFinalText, true);
        m_pendingFinalText.clear();
    }

    if (!message.trimmed().isEmpty())
    {
        m_chatWindow->appendAiMessageStart();
        m_chatWindow->finalizeAssistantMessage(message, true);
    }

    m_runtimeBusyRequested = false;
    syncChatBusyUi();
}


void ChatController::onModelChanged(const QString& modelFolder, const QString& modelDir)
{
    m_modelFolder = modelFolder;
    m_modelDir = modelDir;

    if (m_toolRegistry)
        m_toolRegistry->clear();
    m_pendingSendModelFolder.clear();
    m_pendingSendText.clear();
    m_hasPendingSend = false;

    // Stop any ongoing work tied to previous model.
    stopPlayback();
    if (m_pacedRevealTimer.isActive()) m_pacedRevealTimer.stop();
    m_pacedFullText.clear();
    m_pacedRevealChars = 0;

    if (m_client) m_client->cancel();
    if (m_tts) m_tts->cancel();
    m_pendingFinalText.clear();
    m_pendingStreamText.clear();
    m_runtimeBusyRequested = false;

    if (m_chatWindow)
    {
        m_chatWindow->setCurrentModel(modelFolder, modelDir);
        m_chatWindow->loadFromDisk(modelFolder);
        syncChatBusyUi();
    }

    if (m_useAgentRuntime && m_runtime)
        m_runtime->setModelContext(modelFolder, modelDir);

    // Apply preferred audio output as well (model switch might happen when devices changed)
    applyPreferredAudioOutput();
}

void ChatController::applyPreferredAudioOutput()
{
    if (!m_audioOut)
        return;

    const QString idB64 = SettingsManager::instance().preferredAudioOutputIdBase64().trimmed();
    if (idB64.isEmpty())
    {
        m_audioOut->setDevice(QMediaDevices::defaultAudioOutput());
        return;
    }

    const QByteArray wantedId = QByteArray::fromBase64(idB64.toLatin1());
    const QList<QAudioDevice> devs = QMediaDevices::audioOutputs();
    for (const QAudioDevice& dev : devs)
    {
        if (dev.id() == wantedId)
        {
            m_audioOut->setDevice(dev);
            return;
        }
    }

    m_audioOut->setDevice(QMediaDevices::defaultAudioOutput());
}

void ChatController::reloadMcpTools()
{
    auto* client = dynamic_cast<OpenAIChatClient*>(m_client);
    if (!client)
        return;

    if (m_mcpToolsReloadInProgress)
        return;

    m_mcpToolsReloadInProgress = true;
    m_mcpToolsDirty = false;

    const QList<McpServerConfig> serverConfigs = SettingsManager::instance().mcpServers();
    const QHash<QString, McpServerConfig> previousConfigCache = m_mcpConfigCache;
    const QHash<QString, QJsonArray> previousRawToolsCache = m_mcpRawToolsCache;
    const QHash<QString, McpServerStatus> previousStatusCache = m_mcpStatusCache;

    QHash<QString, McpServerConfig> nextConfigCache;
    QHash<QString, QJsonArray> nextRawToolsCache;
    QHash<QString, McpServerStatus> pendingStatusCache;
    QList<McpServerConfig> serversToReload;
    int enabledServerCount = 0;

    nextConfigCache.reserve(serverConfigs.size());
    pendingStatusCache.reserve(serverConfigs.size());

    for (const McpServerConfig& serverConfig : serverConfigs)
    {
        const QString serverName = serverConfig.name.trimmed();
        if (serverName.isEmpty())
            continue;

        nextConfigCache.insert(serverName, serverConfig);

        McpServerStatus status;
        status.name = serverName;
        status.enabled = serverConfig.enabled;

        if (!serverConfig.enabled)
        {
            status.state = McpServerRuntimeState::Disabled;
            pendingStatusCache.insert(serverName, status);
            continue;
        }

        ++enabledServerCount;

        const bool sameConfig = previousConfigCache.contains(serverName)
            && sameMcpServerConfig(previousConfigCache.value(serverName), serverConfig);
        const bool hasPreviousTools = previousRawToolsCache.contains(serverName);
        const bool hasPreviousStatus = previousStatusCache.contains(serverName);

        if (sameConfig && hasPreviousTools)
        {
            nextRawToolsCache.insert(serverName, previousRawToolsCache.value(serverName));
            status = hasPreviousStatus ? previousStatusCache.value(serverName) : status;
            status.name = serverName;
            status.enabled = true;
            if (status.state == McpServerRuntimeState::Starting)
            {
                status.state = McpServerRuntimeState::Enabled;
                status.detail.clear();
            }
            pendingStatusCache.insert(serverName, status);
            continue;
        }

        status.state = McpServerRuntimeState::Starting;
        pendingStatusCache.insert(serverName, status);
        serversToReload.push_back(serverConfig);
    }

    QList<McpServerStatus> pendingStatuses = buildMcpStatusesFromCache(serverConfigs, pendingStatusCache);
    publishMcpServerStatuses(pendingStatuses);

    QPointer<ChatController> guarded(this);

    QThread* reloadThread = QThread::create([guarded,
                                             serverConfigs,
                                             serversToReload,
                                             enabledServerCount,
                                             nextConfigCache,
                                             nextRawToolsCache,
                                             pendingStatusCache]() mutable {
        QJsonArray mergedTools;
        QHash<QString, QPair<QString, QString>> toolRoutes;
        QHash<QString, QJsonArray> finalRawToolsCache = nextRawToolsCache;
        QHash<QString, McpServerStatus> finalStatusCache = pendingStatusCache;

        struct ServerLoadResult
        {
            QString serverName;
            QJsonArray tools;
            QString error;
        };

        for (const McpServerConfig& serverConfig : serversToReload)
        {
            ServerLoadResult loaded;
            loaded.serverName = serverConfig.name;

            QString initError;
            std::unique_ptr<IMcpAdapter> adapter = IMcpAdapter::create(serverConfig, &initError);
            if (!adapter)
            {
                loaded.error = initError.isEmpty()
                    ? QStringLiteral("create adapter failed")
                    : initError;
            }
            else
            {
                QString toolsError;
                loaded.tools = adapter->listTools(&toolsError);
                if (!toolsError.isEmpty())
                    loaded.error = toolsError;
            }

            if (finalStatusCache.contains(loaded.serverName))
            {
                McpServerStatus status = finalStatusCache.value(loaded.serverName);
                status.enabled = true;
                status.state = loaded.error.isEmpty() ? McpServerRuntimeState::Enabled : McpServerRuntimeState::Unavailable;
                status.detail = loaded.error;
                finalStatusCache.insert(loaded.serverName, status);
            }

            if (!loaded.error.isEmpty())
            {
                finalRawToolsCache.remove(loaded.serverName);
                qWarning().noquote() << "[MCP] list tools failed:" << loaded.serverName << loaded.error;
                continue;
            }

            finalRawToolsCache.insert(loaded.serverName, loaded.tools);
        }

        composeMergedMcpTools(serverConfigs, finalRawToolsCache, &mergedTools, &toolRoutes);
        const QList<McpServerStatus> finalStatuses = buildMcpStatusesFromCache(serverConfigs, finalStatusCache);

        QMetaObject::invokeMethod(qApp, [guarded,
                                         mergedTools,
                                         enabledServerCount,
                                         toolRoutes,
                                         finalStatuses,
                                         nextConfigCache,
                                         finalRawToolsCache,
                                         finalStatusCache] {
            if (!guarded)
                return;
            if (guarded->m_toolRegistry)
            {
                guarded->m_toolRegistry->clear();
                for (auto it = toolRoutes.begin(); it != toolRoutes.end(); ++it)
                    guarded->m_toolRegistry->registerRoute(it.key(), it.value().first, it.value().second);
            }
            guarded->applyMcpTools(mergedTools,
                                   enabledServerCount,
                                   finalStatuses,
                                   nextConfigCache,
                                   finalRawToolsCache,
                                   finalStatusCache);
        }, Qt::QueuedConnection);
    });
    QObject::connect(reloadThread, &QThread::finished, reloadThread, &QObject::deleteLater);
    reloadThread->start();
}

void ChatController::applyMcpTools(const QJsonArray& tools,
                                   int enabledServerCount,
                                   const QList<McpServerStatus>& statuses,
                                   const QHash<QString, McpServerConfig>& configCache,
                                   const QHash<QString, QJsonArray>& rawToolsCache,
                                   const QHash<QString, McpServerStatus>& statusCache)
{
    auto* client = dynamic_cast<OpenAIChatClient*>(m_client);
    if (!client)
        return;

    m_mcpToolsReloadInProgress = false;
    if (m_mcpToolsDirty)
    {
        reloadMcpTools();
        return;
    }

    m_mcpAdapters.clear();
    m_mcpConfigCache = configCache;
    m_mcpRawToolsCache = rawToolsCache;
    m_mcpStatusCache = statusCache;
    const QJsonArray mergedTools = tools;
    client->setTools(mergedTools);
    publishMcpServerStatuses(statuses);
    qDebug().noquote() << "[MCP] tools loaded from servers:" << enabledServerCount << "tool count:" << mergedTools.size();

    if (m_hasPendingSend)
    {
        const QString pendingModelFolder = m_pendingSendModelFolder;
        const QString pendingText = m_pendingSendText;
        m_pendingSendModelFolder.clear();
        m_pendingSendText.clear();
        m_hasPendingSend = false;
        dispatchSendNow(pendingModelFolder, pendingText);
    }
}

void ChatController::markMcpToolsDirty()
{
    m_mcpToolsDirty = true;
    publishMcpServerStatuses(buildMcpStatusesFromCache(SettingsManager::instance().mcpServers(), m_mcpStatusCache));
    if (!m_mcpToolsReloadInProgress)
        reloadMcpTools();
}

void ChatController::refreshMcpServerStatuses()
{
    if (m_mcpToolsReloadInProgress)
        return;

    reloadMcpTools();
}

void ChatController::setMcpServerEnabled(const QString& serverName, bool enabled)
{
    if (serverName.trimmed().isEmpty())
        return;

    SettingsManager& settings = SettingsManager::instance();
    if (!settings.hasMcpServer(serverName))
        return;

    McpServerConfig config = settings.mcpServer(serverName);
    if (config.name.trimmed().isEmpty())
        return;

    if (config.enabled == enabled)
    {
        refreshMcpServerStatuses();
        return;
    }

    config.enabled = enabled;
    settings.updateMcpServer(config);
    markMcpToolsDirty();
}

void ChatController::onRuntimeRequestStartTts(const QString& text, const QString& audioPath, bool streamingEnabled)
{
    // Runtime is requesting TTS generation.
    if (!m_tts)
        return;

    Q_UNUSED(streamingEnabled);  // Used by Runtime internally.
    m_tts->startSpeech(text, audioPath);
}

void ChatController::onRuntimeRequestStartPlayback(const QString& audioPath)
{
    // Runtime is requesting playback of generated audio.
    if (!audioPath.isEmpty())
        startPlaybackWithLip(audioPath);
}

void ChatController::onRuntimeRequestStartPacedTextReveal(const QString& fullText)
{
    // Runtime is requesting paced text reveal (sync with audio).
    startPacedTextReveal(fullText);
}

void ChatController::onRuntimeToolCallRequested(const QString& toolName, const QString& toolInput)
{
    if (!m_runtime)
        return;

    QString resolvedServerName;
    QString resolvedRawToolName;
    if (m_toolRegistry)
        m_toolRegistry->resolveRoute(toolName, &resolvedServerName, &resolvedRawToolName);

    QPointer<ChatController> guarded(this);
    QThread* toolThread = QThread::create([guarded, toolName, toolInput, resolvedServerName, resolvedRawToolName] {
        QString errorMessage;
        QString result;

        QString serverName = resolvedServerName;
        QString rawToolName = resolvedRawToolName;
        const QList<McpServerConfig> serverConfigs = SettingsManager::instance().mcpServers();

        if (serverName.isEmpty())
            tryResolveFromExposedNameHeuristic(toolName, serverConfigs, &serverName, &rawToolName);

        if (serverName.isEmpty())
        {
            const int sep = toolName.indexOf(QLatin1Char(':'));
            if (sep > 0 && sep < toolName.size() - 1)
            {
                serverName = toolName.left(sep);
                rawToolName = toolName.mid(sep + 1);
            }
        }

        if (serverName.isEmpty() || rawToolName.isEmpty())
        {
            errorMessage = QStringLiteral("Tool name must include MCP server prefix, e.g. server__tool");
        }
        else
        {
            McpServerConfig selectedConfig;
            bool found = false;
            for (const McpServerConfig& cfg : serverConfigs)
            {
                if (cfg.name == serverName)
                {
                    selectedConfig = cfg;
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                errorMessage = QStringLiteral("MCP server not found: ") + serverName;
            }
            else if (!selectedConfig.enabled)
            {
                errorMessage = QStringLiteral("MCP server is disabled: ") + serverName;
            }
            else
            {
                QString initError;
                std::unique_ptr<IMcpAdapter> adapter = IMcpAdapter::create(selectedConfig, &initError);
                if (!adapter)
                {
                    errorMessage = initError.isEmpty()
                        ? (QStringLiteral("MCP adapter init failed: ") + serverName)
                        : initError;
                }
                else
                {
                    result = adapter->callTool(rawToolName, toolInput, &errorMessage);
                    if (errorMessage.isEmpty())
                    {
                        const QString preview = result.left(180).replace('\n', QLatin1Char(' '));
                        qInfo().noquote() << "[MCP] tool call success:" << serverName + QLatin1Char(':') + rawToolName
                                          << "result_preview:" << preview;
                    }
                }
            }
        }

        if (!errorMessage.isEmpty())
        {
            qWarning().noquote() << "[MCP] tool call failed:" << toolName << errorMessage;
            result = QStringLiteral("[MCP error] ") + errorMessage;
        }

        QMetaObject::invokeMethod(qApp, [guarded, toolName, toolInput, result] {
            if (!guarded || !guarded->m_runtime)
                return;
            guarded->m_runtime->submitToolResult(toolName, toolInput, result);
        }, Qt::QueuedConnection);
    });
    QObject::connect(toolThread, &QThread::finished, toolThread, &QObject::deleteLater);
    toolThread->start();
}

bool ChatController::hasActiveOutputPresentation() const
{
    const bool ttsGenerating = m_tts && m_tts->isBusy();
    const bool audioPlaying = m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
    const bool pacedReveal = m_pacedRevealTimer.isActive();
    return ttsGenerating || audioPlaying || pacedReveal;
}

void ChatController::syncChatBusyUi()
{
    if (!m_chatWindow)
        return;

    const bool busy = m_runtimeBusyRequested || hasActiveOutputPresentation();
    m_chatWindow->setBusy(busy);
}

void ChatController::publishMcpServerStatuses(const QList<McpServerStatus>& statuses)
{
    m_mcpStatuses = statuses;
    emit mcpServerStatusesChanged(m_mcpStatuses);
}
