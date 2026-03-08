#pragma once
#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QRect>

struct ModelEntry {
    QString folderName;   // e.g. "Hiyori"
    QString jsonPath;     // absolute path to *.model3.json
};

class SettingsManager {
public:
    static SettingsManager& instance();

    QString configDir() const;
    QString configPath() const;
    QString modelsRoot() const;
    void setModelsRoot(const QString& p);
    QString defaultModelsRoot() const;
    void resetModelsRootToDefault(const QString& appDir);

    QVector<ModelEntry> scanModels() const;
    QString selectedModelFolder() const;
    void setSelectedModelFolder(const QString& name);

    QString themeMode() const;
    void setThemeMode(const QString& mode);

    // UI language code (e.g. zh_CN / en_US)
    QString currentLanguage() const;
    void setCurrentLanguage(const QString& code);

    bool hasWindowGeometry() const;
    QRect windowGeometry() const;
    void setWindowGeometry(const QRect& r);

    // The screen signature used when window geometry was last saved.
    // Used to detect display changes and reset window geometry when needed.
    QString windowGeometryScreen() const;
    void setWindowGeometryScreen(const QString& sig);

    void load();
    void save() const;
    void bootstrap(const QString& appDir);

    QString modelConfigPath(const QString& modelFolder) const;

    bool enableBlink() const;
    bool enableBreath() const;
    bool enableGaze() const;
    bool enablePhysics() const;

    void setEnableBlink(bool v);
    void setEnableBreath(bool v);
    void setEnableGaze(bool v);
    void setEnablePhysics(bool v);

    QString watermarkExpPath() const;
    void setWatermarkExpPath(const QString& absPath);

    int textureMaxDim() const;
    void setTextureMaxDim(int dim);
    int msaaSamples() const;
    void setMsaaSamples(int samples);

    int poseAB() const;
    void setPoseAB(int index);

    void ensureModelConfigExists(const QString& folder) const;

    // ---- AI settings (stored in global config.json) ----
    QString aiBaseUrl() const;        // user input like https://xxx/v1
    void setAiBaseUrl(const QString& url);
    QString aiApiKey() const;
    void setAiApiKey(const QString& key);
    QString aiModel() const;
    void setAiModel(const QString& model);
    QString aiSystemPrompt() const;
    void setAiSystemPrompt(const QString& prompt);
    bool aiStreamEnabled() const;
    void setAiStreamEnabled(bool enabled);

    // ---- TTS settings (stored in global config.json) ----
    QString ttsBaseUrl() const; // user input like https://xxx/v1 (we'll append /audio/speech)
    void setTtsBaseUrl(const QString& url);
    QString ttsApiKey() const;
    void setTtsApiKey(const QString& key);
    QString ttsModel() const;
    void setTtsModel(const QString& model);
    QString ttsVoice() const;
    void setTtsVoice(const QString& voice);

    // Cache dir for temp files (e.g. TTS audio)
    QString cacheDir() const;

    // Chat persistence (per model, stored under the platform-specific app data directory)
    QString chatsDir() const;
    QString chatPathForModel(const QString& modelFolder) const; // xxxx.chat.json

    // ---- Advanced runtime selection (stored in global config.json) ----
    // Empty means: follow system default.
    QString preferredScreenName() const;
    void setPreferredScreenName(const QString& name);

    // Store QAudioDevice::id() as Base64 for JSON.
    // Empty means: follow system default.
    QString preferredAudioOutputIdBase64() const;
    void setPreferredAudioOutputIdBase64(const QString& idBase64);

private:
    SettingsManager() = default;

    QString m_modelsRoot;
    QString m_selectedFolder;
    QString m_themeMode{"system"};
    QString m_currentLanguage;
    int m_winX{-1}, m_winY{-1}, m_winW{0}, m_winH{0};
    QString m_winScreen; // screen signature used when saving window geometry
    int m_textureMaxDim{2048};
    int m_msaaSamples{4};

    // Advanced
    QString m_preferredScreenName;
    QString m_preferredAudioOutputIdB64;

    // AI
    QString m_aiBaseUrl;
    QString m_aiApiKey;
    QString m_aiModel{"gpt-4o-mini"};
    QString m_aiSystemPrompt;
    bool m_aiStream{true};

    // TTS
    QString m_ttsBaseUrl;
    QString m_ttsApiKey;
    QString m_ttsModel{"gpt-4o-mini-tts"};
    QString m_ttsVoice{"alloy"};

    QJsonObject loadModelConfigObject(const QString& folder) const;
    void saveModelConfigObject(const QString& folder, const QJsonObject& obj) const;
};

