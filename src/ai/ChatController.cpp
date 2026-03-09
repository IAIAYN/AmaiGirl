#include "ai/ChatController.hpp"

#include "ai/OpenAIChatClient.hpp"
#include "ai/OpenAITtsClient.hpp"

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

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
QJsonArray loadMessages(const QString& modelFolder)
{
    QFile f(SettingsManager::instance().chatPathForModel(modelFolder));
    if (!f.exists()) return {};
    if (!f.open(QIODevice::ReadOnly)) return {};
    QJsonParseError e;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return doc.object().value("messages").toArray();
}

void saveMessages(const QString& modelFolder, const QJsonArray& messages)
{
    QDir dir(SettingsManager::instance().chatsDir());
    if (!dir.exists()) dir.mkpath(".");

    QJsonObject o;
    o["messages"] = messages;

    QFile f(SettingsManager::instance().chatPathForModel(modelFolder));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    }
}

QString latestAssistantMessageFromDisk(const QString& modelFolder)
{
    QFile f(SettingsManager::instance().chatPathForModel(modelFolder));
    if (!f.exists()) return {};
    if (!f.open(QIODevice::ReadOnly)) return {};

    QJsonParseError e;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) return {};

    const QJsonArray messages = doc.object().value("messages").toArray();
    for (int i = messages.size() - 1; i >= 0; --i)
    {
        const auto o = messages.at(i).toObject();
        if (o.value("role").toString() == QStringLiteral("assistant"))
        {
            return o.value("content").toString();
        }
    }
    return {};
}
} // namespace

ChatController::ChatController(QObject* parent)
    : QObject(parent)
{
    m_lipDebug = (QProcessEnvironment::systemEnvironment().value(QStringLiteral("AMAI_LIPSYNC_DEBUG")) == QStringLiteral("1"));

    m_client = new OpenAIChatClient(this);
    m_tts = new OpenAITtsClient(this);

    connect(m_client, &OpenAIChatClient::tokenReceived, this, &ChatController::onClientToken);
    connect(m_client, &OpenAIChatClient::finished, this, &ChatController::onClientFinished);
    connect(m_client, &OpenAIChatClient::errorOccurred, this, &ChatController::onClientError);

    connect(m_tts, &OpenAITtsClient::finished, this, &ChatController::onTtsFinished);
    connect(m_tts, &OpenAITtsClient::errorOccurred, this, &ChatController::onTtsError);

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

            // Ensure UI unlocks when audio finishes.
            if (m_chatWindow) m_chatWindow->setBusy(false);
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
            // Final persistence must happen once (on playback stop).
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

    connect(m_chatWindow, &ChatWindow::requestSendMessage, this, &ChatController::onSendRequested);
    connect(m_chatWindow, &ChatWindow::requestClearChat, this, &ChatController::onClearRequested);

    // sync current model context to window
    if (!m_modelFolder.isEmpty())
        m_chatWindow->setCurrentModel(m_modelFolder, m_modelDir);
}

void ChatController::setRenderer(Renderer* renderer)
{
    m_renderer = renderer;
}

void ChatController::refreshTtsConfig()
{
    OpenAITtsClient::TtsConfig cfg;
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

    for (qsizetype i = start; i < end; ++i)
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
    if (!SettingsManager::instance().aiStreamEnabled()) return;

    // If TTS is enabled, we pace text streaming with audio playback (see onTtsFinished)
    // to avoid text finishing before speech starts.
    refreshTtsConfig();
    const auto tcfg = m_tts->config();
    const bool ttsEnabled = !tcfg.baseUrl.trimmed().isEmpty();

    if (ttsEnabled)
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

    // If TTS is enabled, defer finalizing/persisting the assistant message until
    // we are ready to start playback. This avoids writing the same assistant
    // message twice (onClientFinished + onTtsFinished).
    refreshTtsConfig();
    const auto tcfg = m_tts->config();
    const bool ttsEnabled = !tcfg.baseUrl.trimmed().isEmpty();

    m_pendingFinalText = fullText;

    if (ttsEnabled)
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

    if (m_client->isBusy() || m_tts->isBusy() || (m_player && m_player->playbackState() == QMediaPlayer::PlayingState))
        return;

    stopPlayback();
    refreshClientConfig();

    // Controller owns appending + persisting the user message (single source of truth).
    m_chatWindow->appendUserMessage(userText);
    m_chatWindow->appendAiMessageStart();
    m_chatWindow->setBusy(true);

    // Build payload from current persisted messages.
    const QByteArray msgJson = QJsonDocument(loadMessages(modelFolder)).toJson(QJsonDocument::Compact);
    m_client->startChat(msgJson);
}

void ChatController::onClearRequested(const QString& modelFolder)
{
    if (!m_chatWindow) return;
    if (modelFolder.isEmpty()) return;

    stopPlayback();
    if (m_pacedRevealTimer.isActive()) m_pacedRevealTimer.stop();
    m_pacedFullText.clear();
    m_pacedRevealChars = 0;

    m_client->cancel();
    m_tts->cancel();
    m_pendingFinalText.clear();
    m_pendingStreamText.clear();

    saveClearedChat(modelFolder);

    m_chatWindow->loadFromDisk(modelFolder);
    m_chatWindow->setBusy(false);
}

void ChatController::refreshClientConfig()
{
    OpenAIChatClient::ChatConfig cfg;
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
    saveMessages(modelFolder, QJsonArray{});
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
        m_chatWindow->setBusy(false);
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

    m_chatWindow->setBusy(false);
}


void ChatController::onModelChanged(const QString& modelFolder, const QString& modelDir)
{
    m_modelFolder = modelFolder;
    m_modelDir = modelDir;

    // Stop any ongoing work tied to previous model.
    stopPlayback();
    if (m_pacedRevealTimer.isActive()) m_pacedRevealTimer.stop();
    m_pacedFullText.clear();
    m_pacedRevealChars = 0;

    if (m_client) m_client->cancel();
    if (m_tts) m_tts->cancel();
    m_pendingFinalText.clear();
    m_pendingStreamText.clear();

    if (m_chatWindow)
    {
        m_chatWindow->setCurrentModel(modelFolder, modelDir);
        m_chatWindow->loadFromDisk(modelFolder);
        m_chatWindow->setBusy(false);
    }

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
