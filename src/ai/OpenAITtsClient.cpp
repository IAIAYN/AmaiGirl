// Defensively undo third-party macro pollution that breaks Qt headers.
#ifdef slots
#  undef slots
#endif

#include "ai/OpenAITtsClient.hpp"

// Qt uses the keyword 'slots' in headers (unless QT_NO_KEYWORDS is set).
// Some third-party SDKs (and even old code) occasionally define a macro named 'slots'
// which breaks Qt headers badly (you'll see errors deep inside Qt about operators
// having the wrong number of parameters). Ensure it's not defined for this TU.
//#ifdef slots
//#  undef slots
//#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>

#include <QtMultimedia/QAudioBuffer>
#include <QtMultimedia/QAudioDecoder>
#include <QtMultimedia/QAudioFormat>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
QString withErrorTag(const QString& message)
{
    if (message.startsWith(QStringLiteral("[Error]"))) return message;
    return QStringLiteral("[Error] %1").arg(message);
}

bool isRiffWave(const QByteArray& data)
{
    return data.size() >= 12
        && QByteArrayView(data.constData(), 4) == "RIFF"
        && QByteArrayView(data.constData() + 8, 4) == "WAVE";
}

void appendLe16(QByteArray* out, quint16 v)
{
    out->append(char(v & 0xFF));
    out->append(char((v >> 8) & 0xFF));
}

void appendLe32(QByteArray* out, quint32 v)
{
    out->append(char(v & 0xFF));
    out->append(char((v >> 8) & 0xFF));
    out->append(char((v >> 16) & 0xFF));
    out->append(char((v >> 24) & 0xFF));
}

QByteArray buildWavFromMonoS16(const QByteArray& pcm, int sampleRate)
{
    const int bitsPerSample = 16;
    const int channels = 1;
    const int byteRate = sampleRate * channels * (bitsPerSample / 8);
    const int blockAlign = channels * (bitsPerSample / 8);
    const quint32 dataSize = quint32(pcm.size());
    const quint32 riffSize = 36u + dataSize;

    QByteArray out;
    out.reserve(int(44u + dataSize));
    out.append("RIFF", 4);
    appendLe32(&out, riffSize);
    out.append("WAVE", 4);
    out.append("fmt ", 4);
    appendLe32(&out, 16u);
    appendLe16(&out, 1u); // PCM
    appendLe16(&out, quint16(channels));
    appendLe32(&out, quint32(sampleRate));
    appendLe32(&out, quint32(byteRate));
    appendLe16(&out, quint16(blockAlign));
    appendLe16(&out, quint16(bitsPerSample));
    out.append("data", 4);
    appendLe32(&out, dataSize);
    out.append(pcm);
    return out;
}

bool transcodeToPcmS16leWav(const QByteArray& sourceData, QByteArray* outWav, QString* err)
{
    if (!outWav)
    {
        if (err) *err = QObject::tr("输出缓冲区无效");
        return false;
    }

    QTemporaryFile inputFile(QDir(QDir::tempPath()).filePath(QStringLiteral("amaigirl-tts-in-XXXXXX.mp3")));
    inputFile.setAutoRemove(true);
    if (!inputFile.open())
    {
        if (err) *err = QObject::tr("无法创建临时输入文件");
        return false;
    }
    if (inputFile.write(sourceData) != sourceData.size())
    {
        if (err) *err = QObject::tr("写入临时输入文件失败");
        return false;
    }
    inputFile.flush();

    QAudioDecoder decoder;

    QAudioFormat wantedFormat;
    wantedFormat.setSampleRate(24000);
    wantedFormat.setChannelCount(1);
    wantedFormat.setSampleFormat(QAudioFormat::Int16);
    decoder.setAudioFormat(wantedFormat);
    decoder.setSource(QUrl::fromLocalFile(inputFile.fileName()));

    QByteArray pcmS16Mono;
    int outputSampleRate = 24000;
    QString decodeErr;
    bool finished = false;

    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &decoder, [&]() {
        const QAudioBuffer buffer = decoder.read();
        if (!buffer.isValid())
            return;

        const QAudioFormat fmt = buffer.format();
        if (!fmt.isValid() || fmt.channelCount() <= 0 || fmt.bytesPerFrame() <= 0)
        {
            decodeErr = QObject::tr("Qt 解码输出格式无效");
            return;
        }

        if (fmt.sampleRate() > 0)
            outputSampleRate = fmt.sampleRate();

        const int frames = buffer.frameCount();
        const int channels = fmt.channelCount();
        const int bytesPerSample = fmt.bytesPerSample();
        if (frames <= 0 || bytesPerSample <= 0)
            return;

        const char* raw = buffer.constData<char>();
        const int frameBytes = fmt.bytesPerFrame();

        auto sampleAt = [&](int frame, int ch) -> float {
            const int off = frame * frameBytes + ch * bytesPerSample;
            if (fmt.sampleFormat() == QAudioFormat::UInt8)
            {
                const quint8 v = *reinterpret_cast<const quint8*>(raw + off);
                return (float(v) - 128.0f) / 128.0f;
            }
            if (fmt.sampleFormat() == QAudioFormat::Int16)
            {
                qint16 v = 0;
                std::memcpy(&v, raw + off, sizeof(qint16));
                return float(v) / 32768.0f;
            }
            if (fmt.sampleFormat() == QAudioFormat::Int32)
            {
                qint32 v = 0;
                std::memcpy(&v, raw + off, sizeof(qint32));
                return float(double(v) / 2147483648.0);
            }
            if (fmt.sampleFormat() == QAudioFormat::Float)
            {
                float v = 0.0f;
                std::memcpy(&v, raw + off, sizeof(float));
                return v;
            }
            return 0.0f;
        };

        pcmS16Mono.reserve(pcmS16Mono.size() + frames * 2);
        for (int i = 0; i < frames; ++i)
        {
            float mono = 0.0f;
            for (int c = 0; c < channels; ++c)
                mono += sampleAt(i, c);
            mono /= float(channels);
            mono = std::clamp(mono, -1.0f, 1.0f);

            const qint16 s = qint16(std::lround(mono * 32767.0f));
            pcmS16Mono.append(char(s & 0xFF));
            pcmS16Mono.append(char((s >> 8) & 0xFF));
        }
    });

    QEventLoop loop;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, [&]() {
        decodeErr = QObject::tr("Qt 音频解码超时");
        decoder.stop();
        loop.quit();
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, [&]() {
        finished = true;
        loop.quit();
    });

    decoder.start();
    watchdog.start(120000);
    loop.exec();

    if (!finished)
    {
        if (decodeErr.isEmpty())
            decodeErr = QObject::tr("Qt 音频解码未完成");
        if (err) *err = decodeErr;
        return false;
    }

    if (decoder.error() != QAudioDecoder::NoError)
    {
        if (err) *err = QObject::tr("Qt 音频解码失败: %1").arg(decoder.errorString());
        return false;
    }

    if (pcmS16Mono.isEmpty())
    {
        if (err) *err = QObject::tr("Qt 音频解码结果为空");
        return false;
    }

    const QByteArray wavData = buildWavFromMonoS16(pcmS16Mono, qMax(1, outputSampleRate));
    if (!isRiffWave(wavData))
    {
        if (err) *err = QObject::tr("Qt 转码结果不是有效 WAV");
        return false;
    }

    *outWav = wavData;
    return true;
}
} // namespace

OpenAITtsClient::OpenAITtsClient(QObject* parent)
    : QObject(parent)
{
}

void OpenAITtsClient::setConfig(TtsConfig cfg)
{
    m_cfg = std::move(cfg);
}

OpenAITtsClient::TtsConfig OpenAITtsClient::config() const
{
    return m_cfg;
}

QString OpenAITtsClient::normalizeBaseUrlToV1(const QString& baseUrl)
{
    QString u = baseUrl.trimmed();
    if (u.endsWith('/'))
    {
        u.chop(1);
    }

    // allow user to input https://host OR https://host/v1
    if (!u.endsWith("/v1"))
    {
        u += "/v1";
    }

    return u;
}

QUrl OpenAITtsClient::makeSpeechUrl(const QString& baseUrlV1)
{
    return QUrl(baseUrlV1 + "/audio/speech");
}

void OpenAITtsClient::startSpeech(const QString& text, const QString& outPath)
{
    if (isBusy())
    {
        emitErrorAndCleanup(tr("TTS 正在忙碌中"));
        return;
    }

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
    {
        emitErrorAndCleanup(tr("TTS 输入为空"));
        return;
    }

    const QString baseV1 = normalizeBaseUrlToV1(m_cfg.baseUrl);
    if (baseV1.isEmpty())
    {
        emitErrorAndCleanup(tr("TTS base_url 为空"));
        return;
    }

    m_outPath = outPath;

    QJsonObject body;
    body.insert("model", m_cfg.model.isEmpty() ? QStringLiteral("gpt-4o-mini-tts") : m_cfg.model);
    body.insert("voice", m_cfg.voice.isEmpty() ? QStringLiteral("alloy") : m_cfg.voice);
    body.insert("input", trimmed);

    // Prefer uncompressed (or lightly compressed) format for lip-sync.
    // OpenAI-compatible endpoints support `response_format`: wav / pcm / mp3.
    body.insert("response_format", QStringLiteral("wav"));

    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkRequest req(makeSpeechUrl(baseV1));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_cfg.apiKey.trimmed().isEmpty())
    {
        req.setRawHeader("Authorization", QByteArray("Bearer ") + m_cfg.apiKey.trimmed().toUtf8());
    }

    m_reply = m_nam.post(req, payload);

    emit started();

    connect(m_reply, &QNetworkReply::finished, this, &OpenAITtsClient::onFinished);
}

void OpenAITtsClient::cancel()
{
    if (m_reply)
    {
        m_reply->abort();
    }
    cleanupReply();
    m_outPath.clear();
}

void OpenAITtsClient::onFinished()
{
    if (!m_reply)
    {
        return;
    }

    const auto httpStatus = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int statusCode = httpStatus.isValid() ? httpStatus.toInt() : 0;

    const QByteArray data = m_reply->readAll();
    const QString errStr = m_reply->errorString();
    const bool ok = (m_reply->error() == QNetworkReply::NoError) && (statusCode >= 200 && statusCode < 300);

    cleanupReply();

    if (!ok)
    {
        QString details;
        if (!data.isEmpty())
        {
            details = QString::fromUtf8(data.left(2048));
        }
        emit errorOccurred(withErrorTag(tr("TTS 请求失败 (%1): %2 %3").arg(statusCode).arg(errStr, details)));
        return;
    }

    if (m_outPath.isEmpty())
    {
        emit errorOccurred(withErrorTag(tr("TTS 输出路径为空")));
        return;
    }

    QByteArray writableData = data;
    if (!isRiffWave(writableData))
    {
        QString transcodeErr;
        QByteArray wavData;
        if (!transcodeToPcmS16leWav(writableData, &wavData, &transcodeErr))
        {
            emit errorOccurred(withErrorTag(tr("TTS 返回非 WAV，且自动转码失败: %1").arg(transcodeErr)));
            return;
        }
        writableData = wavData;
    }

    QDir().mkpath(QFileInfo(m_outPath).absolutePath());

    // atomic write to avoid partial file
    QSaveFile sf(m_outPath);
    if (!sf.open(QIODevice::WriteOnly))
    {
        emit errorOccurred(withErrorTag(tr("无法打开缓存文件进行写入")));
        return;
    }
    sf.write(writableData);
    if (!sf.commit())
    {
        emit errorOccurred(withErrorTag(tr("无法提交缓存文件")));
        return;
    }

    emit finished(m_outPath);
}

void OpenAITtsClient::emitErrorAndCleanup(const QString& message)
{
    cancel();
    emit errorOccurred(withErrorTag(message));
}

void OpenAITtsClient::cleanupReply()
{
    if (!m_reply)
    {
        return;
    }

    m_reply->deleteLater();
    m_reply = nullptr;
}
