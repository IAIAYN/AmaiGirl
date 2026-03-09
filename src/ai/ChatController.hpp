#pragma once

// Some third-party headers define macros like 'slots' that break Qt headers.
// We MUST NOT define QT_NO_KEYWORDS here, otherwise Qt's 'signals/slots' keywords
// are disabled and moc output won't compile.

#include <QObject>
#include <QTimer>
#include <QVector>
#include <QString>
#include <QElapsedTimer>

class OpenAIChatClient;
class OpenAITtsClient;
class ChatWindow;
class Renderer;
class QAudioOutput;
class QMediaPlayer;

class ChatController : public QObject
{
    Q_OBJECT
public:
    explicit ChatController(QObject* parent = nullptr);

    void setChatWindow(ChatWindow* wnd);
    void setRenderer(Renderer* renderer);

public slots:
    void onModelChanged(const QString& modelFolder, const QString& modelDir);
    void applyPreferredAudioOutput();

private slots:
    void onSendRequested(const QString& modelFolder, const QString& userText);
    void onClearRequested(const QString& modelFolder);

    void onClientToken(const QString& token);
    void onClientFinished(const QString& fullText);
    void onClientError(const QString& message);

    void onTtsFinished(const QString& outPath);
    void onTtsError(const QString& message);

    void onLipTimer();

private:
    void refreshClientConfig();
    void refreshTtsConfig();
    void saveClearedChat(const QString& modelFolder) const;

    QString cacheAudioPathForModel(const QString& modelFolder) const;

    void stopPlayback();
    void startPlaybackWithLip(const QString& audioPath);
    void releasePlaybackSource();

    // --- WAV lipsync (WavHandler-like) ---
    bool loadWavForLipSync(const QString& wavPath);
    void startWavLipSyncClock();
    void stopWavLipSyncClock();
    float updateLipRmsFromClock();

    QVector<float> buildLipFramesFromWavFile(const QString& wavPath) const;
    QVector<float> makeFallbackLipFrames() const;

    void maybeDebugLip(const char* tag, float rms, float outV);

    // --- paced text reveal (sync streaming text with TTS playback) ---
    void startPacedTextReveal(const QString& fullText);

private:
    OpenAIChatClient* m_client{};
    OpenAITtsClient* m_tts{};
    ChatWindow* m_chatWindow{};
    Renderer* m_renderer{};

    QAudioOutput* m_audioOut{};
    QMediaPlayer* m_player{};

    QString m_modelFolder;
    QString m_modelDir;

    quint64 m_reqId{0};
    quint64 m_ttsReqId{0};

    QString m_pendingFinalText;

    QTimer* m_textStreamTimer{nullptr};
    QVector<QString> m_streamTokens;
    int m_streamTokenIndex{0};
    double m_streamTokensPerTick{0.0};
    double m_streamAccumulator{0.0};

    QTimer m_lipTimer;

    struct WavLipState
    {
        QByteArray pcm;
        int channels{0};
        int sampleRate{0};
        int bitsPerSample{0};
        int audioFormat{0};
        qsizetype totalFrames{0};
        qsizetype lastOffset{0};
        float currentRms{0.0f};
        bool loaded{false};

        void reset()
        {
            pcm.clear();
            channels = sampleRate = bitsPerSample = audioFormat = 0;
            totalFrames = lastOffset = 0;
            currentRms = 0.0f;
            loaded = false;
        }
    };

    WavLipState m_wavLip;

    // WavHandler-like timebase
    QElapsedTimer m_wavClock;
    bool m_wavClockStarted{false};

    bool m_lipSyncActiveForPlayback{false};
    bool m_preparingPlayback{false};

    float m_lipGain{1.0f};

    // debug switch: set AMAI_LIPSYNC_DEBUG=1 in env
    bool m_lipDebug{false};

    // legacy frames fallback
    QVector<float> m_lipFrames;
    int m_lipFrameIndex{0};

    QString m_pendingStreamText;          // collected tokens while waiting for TTS
    QString m_pacedFullText;              // final text that will be revealed
    int m_pacedRevealChars{0};            // already revealed chars
    QTimer m_pacedRevealTimer;            // reveal timer
    bool m_pacedBubbleStarted{false};     // whether we already started the draft bubble for paced reveal
};
