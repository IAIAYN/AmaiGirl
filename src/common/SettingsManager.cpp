#include "common/SettingsManager.hpp"
#include "common/Utils.hpp"
#include "ai/core/McpServerConfig.hpp"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QStandardPaths>
#include <QLocale>
#include <QDirIterator>
#include <QtGlobal>

#include <algorithm>

static bool copyDirectoryRecursively(const QString& srcPath, const QString& dstPath)
{
    QDir src(srcPath);
    if (!src.exists()) return false;

    QDir dst(dstPath);
    if (!dst.exists() && !dst.mkpath(".")) return false;

    QDirIterator it(srcPath, QDir::NoDotAndDotDot | QDir::AllEntries, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        const QString rel = src.relativeFilePath(fi.absoluteFilePath());
        const QString target = dst.filePath(rel);

        if (fi.isDir()) {
            if (!QDir().mkpath(target)) return false;
            continue;
        }

        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile::remove(target);
        if (!QFile::copy(fi.absoluteFilePath(), target)) return false;
    }
    return true;
}

static QString normalizeLanguageCode(const QString& code)
{
    const QString c = code.trimmed();
    if (c.startsWith("zh", Qt::CaseInsensitive)) return QStringLiteral("zh_CN");
    if (c.startsWith("en", Qt::CaseInsensitive)) return QStringLiteral("en_US");
    return QStringLiteral("en_US");
}

static QString normalizeThemeId(const QString& themeId)
{
    QString id = themeId.trimmed().toLower();
    if (id.isEmpty() || id == QStringLiteral("system"))
        return QStringLiteral("era");
    return id;
}

static QString systemLanguageCode()
{
    const QString n = QLocale::system().name();
    return normalizeLanguageCode(n);
}

static QString writableLocationOrFallback(QStandardPaths::StandardLocation location, const QString& fallback)
{
    const QString path = QStandardPaths::writableLocation(location);
    return path.isEmpty() ? fallback : path;
}

static QString appConfigBaseDir()
{
#if defined(Q_OS_WIN32)
    return writableLocationOrFallback(
        QStandardPaths::AppConfigLocation,
        QDir(QDir::homePath()).filePath(QStringLiteral("AppData/Roaming/IAIAYN/AmaiGirl"))
    );
#else
    return QDir(QDir::homePath()).filePath(QStringLiteral(".AmaiGirl"));
#endif
}

static QString appLocalDataBaseDir()
{
#if defined(Q_OS_WIN32)
    return writableLocationOrFallback(
        QStandardPaths::AppLocalDataLocation,
        QDir(QDir::homePath()).filePath(QStringLiteral("AppData/Local/IAIAYN/AmaiGirl"))
    );
#else
    return QDir(QDir::homePath()).filePath(QStringLiteral(".AmaiGirl"));
#endif
}

SettingsManager& SettingsManager::instance()
{
    static SettingsManager s;
    return s;
}

QString SettingsManager::configDir() const
{
    return QDir(appConfigBaseDir()).filePath(QStringLiteral("Configs"));
}

QString SettingsManager::configPath() const
{
    return QDir(configDir()).filePath("config.json");
}

QString SettingsManager::defaultModelsRoot() const
{
#if defined(Q_OS_WIN32)
    const QString documentsDir = writableLocationOrFallback(QStandardPaths::DocumentsLocation, QDir::homePath());
    return QDir(documentsDir).filePath(QStringLiteral("AmaiGirl/Models"));
#else
    return QDir(QDir::homePath()).filePath(".AmaiGirl/Models");
#endif
}

QString SettingsManager::modelsRoot() const
{
    return m_modelsRoot.isEmpty() ? defaultModelsRoot() : m_modelsRoot;
}

void SettingsManager::setModelsRoot(const QString& p)
{
    m_modelsRoot = p;
    save();
}

void SettingsManager::resetModelsRootToDefault(const QString& appDir)
{
    m_modelsRoot.clear();
    save();
    bootstrap(appDir);
}

QVector<ModelEntry> SettingsManager::scanModels() const
{
    QVector<ModelEntry> out;
    QDir root(modelsRoot());
    if (!root.exists()) return out;

    for (const QFileInfo& fi : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
        QDir sub(fi.absoluteFilePath());
        QStringList jsons = sub.entryList(QStringList{"*.model3.json"}, QDir::Files);
        if (!jsons.isEmpty())
        {
            out.push_back(ModelEntry{ fi.fileName(), sub.filePath(jsons.front()) });
        }
    }
    return out;
}

QString SettingsManager::selectedModelFolder() const
{
    return m_selectedFolder;
}

void SettingsManager::setSelectedModelFolder(const QString& name)
{
    m_selectedFolder = name;
    save();
}

QString SettingsManager::theme() const
{
    return m_theme;
}

void SettingsManager::setTheme(const QString& themeId)
{
    m_theme = normalizeThemeId(themeId);
    save();
}

QString SettingsManager::currentLanguage() const
{
    if (m_currentLanguage.isEmpty()) return systemLanguageCode();
    return normalizeLanguageCode(m_currentLanguage);
}

void SettingsManager::setCurrentLanguage(const QString& code)
{
    m_currentLanguage = normalizeLanguageCode(code);
    save();
}

bool SettingsManager::hasWindowGeometry() const
{
    return m_winW > 0 && m_winH > 0;
}

QRect SettingsManager::windowGeometry() const
{
    return QRect(m_winX, m_winY, m_winW, m_winH);
}

QString SettingsManager::windowGeometryScreen() const
{
    return m_winScreen;
}

void SettingsManager::setWindowGeometryScreen(const QString& sig)
{
    m_winScreen = sig;
    save();
}

void SettingsManager::setWindowGeometry(const QRect& r)
{
    m_winX = r.x();
    m_winY = r.y();
    m_winW = r.width();
    m_winH = r.height();
    save();
}

QString SettingsManager::cacheDir() const
{
    const QString dir = QDir(appLocalDataBaseDir()).filePath(QStringLiteral(".Cache"));
    QDir().mkpath(dir);
    return dir;
}

QString SettingsManager::ttsBaseUrl() const { return m_ttsBaseUrl; }
void SettingsManager::setTtsBaseUrl(const QString& url) { m_ttsBaseUrl = url; save(); }
QString SettingsManager::ttsApiKey() const { return m_ttsApiKey; }
void SettingsManager::setTtsApiKey(const QString& key) { m_ttsApiKey = key; save(); }
QString SettingsManager::ttsModel() const { return m_ttsModel; }
void SettingsManager::setTtsModel(const QString& model) { m_ttsModel = model; save(); }
QString SettingsManager::ttsVoice() const { return m_ttsVoice; }
void SettingsManager::setTtsVoice(const QString& voice) { m_ttsVoice = voice; save(); }

QString SettingsManager::preferredScreenName() const { return m_preferredScreenName; }
void SettingsManager::setPreferredScreenName(const QString& name)
{
    m_preferredScreenName = name.trimmed();
    save();
}

QString SettingsManager::preferredAudioOutputIdBase64() const { return m_preferredAudioOutputIdB64; }
void SettingsManager::setPreferredAudioOutputIdBase64(const QString& idBase64)
{
    m_preferredAudioOutputIdB64 = idBase64.trimmed();
    save();
}

void SettingsManager::load()
{
    QDir dir(configDir());
    if (!dir.exists()) dir.mkpath(".");

    QFile f(configPath());
    if (!f.exists()) { save(); return; }
    if (!f.open(QIODevice::ReadOnly)) return;

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) return;

    auto root = doc.object();
    bool needsSave = false;
    m_modelsRoot     = root.value("modelsRoot").toString(modelsRoot());
    m_selectedFolder = root.value("selectedModelFolder").toString();
    if (root.contains("theme"))
    {
        const QString rawTheme = root.value("theme").toString();
        m_theme = normalizeThemeId(rawTheme);
        if (m_theme != rawTheme)
            needsSave = true;
    }
    else
    {
        m_theme = normalizeThemeId(root.value("themeMode").toString(QStringLiteral("era")));
        if (root.contains("themeMode"))
            needsSave = true;
    }
    if (root.contains("currentLanguage"))
        m_currentLanguage = normalizeLanguageCode(root.value("currentLanguage").toString());
    else
        m_currentLanguage = systemLanguageCode();
    m_winX           = root.value("winX").toInt(-1);
    m_winY           = root.value("winY").toInt(-1);
    m_winW           = root.value("winW").toInt(0);
    m_winH           = root.value("winH").toInt(0);
    m_winScreen      = root.value("winScreen").toString();
    m_textureMaxDim  = root.value("textureMaxDim").toInt(2048);
    m_msaaSamples    = root.value("msaaSamples").toInt(4);

    // Advanced runtime selection
    m_preferredScreenName = root.value("preferredScreen").toString();
    m_preferredAudioOutputIdB64 = root.value("preferredAudioOutput").toString();

    // AI settings
    m_aiBaseUrl      = root.value("aiBaseUrl").toString();
    m_aiApiKey       = root.value("aiApiKey").toString();
    m_aiModel        = root.value("aiModel").toString(m_aiModel);
    m_aiSystemPrompt = root.value("aiSystemPrompt").toString();
    m_aiStream       = root.value("aiStream").toBool(true);

    m_mcpEnabledStates.clear();
    if (root.value("mcpServerEnabledStates").isObject())
    {
        const QJsonObject statesObj = root.value("mcpServerEnabledStates").toObject();
        for (auto it = statesObj.begin(); it != statesObj.end(); ++it)
            m_mcpEnabledStates.insert(it.key(), it.value().toBool(true));
    }

    // Migration: Check if old MCP config exists in config.json, migrate to mcp.json if needed
    if (root.contains("mcp") && root.value("mcp").isObject() && !QFile(mcpJsonPath()).exists())
    {
        const QJsonObject mcp = root.value("mcp").toObject();
        if (mcp.value("enabled").toBool(false)) {
            // Migrate old stdio MCP config to new format
            McpServerConfig oldConfig;
            oldConfig.name = QStringLiteral("Default");
            oldConfig.type = McpServerConfig::Type::Stdio;
            oldConfig.enabled = true;
            oldConfig.command = mcp.value("command").toString();
            oldConfig.args = mcp.value("args").toString();
            oldConfig.timeoutMs = mcp.value("timeoutMs").toInt(8000);
            if (oldConfig.timeoutMs < 1000)
                oldConfig.timeoutMs = 1000;
            m_mcpEnabledStates.insert(oldConfig.name, true);
            m_mcpServers.append(oldConfig);
            saveMcpServersToFile();
        }
        needsSave = true;
    }

    // Load MCP servers from mcp.json
    loadMcpServersFromFile();

    // TTS
    if (root.contains("tts") && root.value("tts").isObject())
    {
        const QJsonObject tts = root.value("tts").toObject();
        m_ttsBaseUrl = tts.value("base_url").toString(m_ttsBaseUrl);
        m_ttsApiKey = tts.value("api_key").toString(m_ttsApiKey);
        m_ttsModel = tts.value("model").toString(m_ttsModel);
        m_ttsVoice = tts.value("voice").toString(m_ttsVoice);
    }

    if (needsSave)
        save();
}

void SettingsManager::save() const
{
    QDir dir(configDir());
    if (!dir.exists()) dir.mkpath(".");

    QJsonObject root;
    root["modelsRoot"]          = m_modelsRoot.isEmpty() ? defaultModelsRoot() : m_modelsRoot;
    root["selectedModelFolder"] = m_selectedFolder;
    root["theme"]               = normalizeThemeId(m_theme);
    root["currentLanguage"]     = currentLanguage();
    root["winX"]                = m_winX;
    root["winY"]                = m_winY;
    root["winW"]                = m_winW;
    root["winH"]                = m_winH;
    root["winScreen"]           = m_winScreen;
    root["textureMaxDim"]       = m_textureMaxDim;
    root["msaaSamples"]         = m_msaaSamples;

    // Advanced runtime selection
    root["preferredScreen"]      = m_preferredScreenName;
    root["preferredAudioOutput"] = m_preferredAudioOutputIdB64;

    // AI settings
    root["aiBaseUrl"]      = m_aiBaseUrl;
    root["aiApiKey"]       = m_aiApiKey;
    root["aiModel"]        = m_aiModel;
    root["aiSystemPrompt"] = m_aiSystemPrompt;
    root["aiStream"]       = m_aiStream;

    QJsonObject mcpEnabledStatesObj;
    for (auto it = m_mcpEnabledStates.begin(); it != m_mcpEnabledStates.end(); ++it)
        mcpEnabledStatesObj.insert(it.key(), it.value());
    root["mcpServerEnabledStates"] = mcpEnabledStatesObj;

    // Note: MCP settings are now stored separately in mcp.json
    // Note: Runtime is always enabled; useAgentRuntime was removed from config

    // TTS
    QJsonObject tts;
    tts.insert("base_url", m_ttsBaseUrl);
    tts.insert("api_key", m_ttsApiKey);
    tts.insert("model", m_ttsModel);
    tts.insert("voice", m_ttsVoice);
    root.insert("tts", tts);

    QFile f(configPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

void SettingsManager::bootstrap(const QString& appDir)
{
    QDir dir(configDir());
    if (!dir.exists()) dir.mkpath(".");

    QDir models(modelsRoot());
    if (!models.exists()) models.mkpath(".");

    // Chats dir
    {
        QDir chats(chatsDir());
        if (!chats.exists()) chats.mkpath(".");
    }

    if (models.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
        const QString bundledModels = appResourcePath(QStringLiteral("models"));
        const QString legacyModels = QDir(appDir).filePath(QStringLiteral("res/models"));
        QStringList candidateSources;
        candidateSources << bundledModels;
#if defined(Q_OS_MACOS)
        candidateSources << QDir(appDir).filePath(QStringLiteral("../Resources/models"));
    #elif defined(Q_OS_LINUX)
        candidateSources << QDir(appDir).filePath(QStringLiteral("../share/AmaiGirl/res/models"));
#endif
        candidateSources << legacyModels;

        bool copied = false;
        for (const QString& src : candidateSources) {
            if (QFileInfo::exists(src) && QFileInfo(src).isDir()) {
                copied = copyDirectoryRecursively(src, models.absolutePath());
                if (copied) break;
            }
        }

        if (copied && m_selectedFolder.isEmpty()) {
            const auto entries = scanModels();
            if (!entries.isEmpty()) {
                m_selectedFolder = entries.front().folderName;
            }
        }

        save();
    }

    if (!m_selectedFolder.isEmpty())
    {
        ensureModelConfigExists(m_selectedFolder);
    }
}

QString SettingsManager::modelConfigPath(const QString& modelFolder) const
{
    if (modelFolder.isEmpty()) return {};
    return QDir(configDir()).filePath(modelFolder + ".config.json");
}

QString SettingsManager::chatsDir() const
{
    return QDir(appLocalDataBaseDir()).filePath(QStringLiteral("Chats"));
}

QString SettingsManager::chatPathForModel(const QString& modelFolder) const
{
    if (modelFolder.isEmpty()) return {};
    return QDir(chatsDir()).filePath(modelFolder + ".chat.json");
}

void SettingsManager::ensureModelConfigExists(const QString& folder) const
{
    if (folder.isEmpty()) return;

    QFile f(modelConfigPath(folder));
    if (f.exists()) return;

    QJsonObject o;
    o["enableBlink"]   = true;
    o["enableBreath"]  = true;
    o["enableGaze"]    = false;
    o["enablePhysics"] = true;
    o["poseAB"]        = 0;

    saveModelConfigObject(folder, o);
}

QJsonObject SettingsManager::loadModelConfigObject(const QString& folder) const
{
    QJsonObject o;
    if (folder.isEmpty()) return o;

    QFile f(modelConfigPath(folder));
    if (!f.exists()) return o;
    if (!f.open(QIODevice::ReadOnly)) return o;

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) return o;

    return doc.object();
}

void SettingsManager::saveModelConfigObject(const QString& folder, const QJsonObject& obj) const
{
    if (folder.isEmpty()) return;

    QDir d(configDir());
    if (!d.exists()) d.mkpath(".");

    QFile f(modelConfigPath(folder));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    }
}

bool SettingsManager::enableBlink() const
{
    return loadModelConfigObject(selectedModelFolder()).value("enableBlink").toBool(true);
}

bool SettingsManager::enableBreath() const
{
    return loadModelConfigObject(selectedModelFolder()).value("enableBreath").toBool(true);
}

bool SettingsManager::enableGaze() const
{
    return loadModelConfigObject(selectedModelFolder()).value("enableGaze").toBool(false);
}

bool SettingsManager::enablePhysics() const
{
    return loadModelConfigObject(selectedModelFolder()).value("enablePhysics").toBool(true);
}

void SettingsManager::setEnableBlink(bool v)
{
    auto f = selectedModelFolder();
    auto o = loadModelConfigObject(f);
    o["enableBlink"] = v;
    saveModelConfigObject(f, o);
}

void SettingsManager::setEnableBreath(bool v)
{
    auto f = selectedModelFolder();
    auto o = loadModelConfigObject(f);
    o["enableBreath"] = v;
    saveModelConfigObject(f, o);
}

void SettingsManager::setEnableGaze(bool v)
{
    auto f = selectedModelFolder();
    auto o = loadModelConfigObject(f);
    o["enableGaze"] = v;
    saveModelConfigObject(f, o);
}

void SettingsManager::setEnablePhysics(bool v)
{
    auto f = selectedModelFolder();
    auto o = loadModelConfigObject(f);
    o["enablePhysics"] = v;
    saveModelConfigObject(f, o);
}

QString SettingsManager::watermarkExpPath() const
{
    return loadModelConfigObject(selectedModelFolder()).value("watermarkExpPath").toString("");
}

void SettingsManager::setWatermarkExpPath(const QString& absPath)
{
    auto f = selectedModelFolder();
    auto o = loadModelConfigObject(f);
    if (absPath.isEmpty()) o.remove("watermarkExpPath");
    else o["watermarkExpPath"] = absPath;
    saveModelConfigObject(f, o);
}

int SettingsManager::textureMaxDim() const
{
    return m_textureMaxDim;
}

void SettingsManager::setTextureMaxDim(int dim)
{
    if (dim != 1024 && dim != 2048 && dim != 3072 && dim != 4096) dim = 2048;
    m_textureMaxDim = dim;
    save();
}

int SettingsManager::msaaSamples() const
{
    return m_msaaSamples;
}

void SettingsManager::setMsaaSamples(int samples)
{
    if (samples != 2 && samples != 4 && samples != 8) samples = 4;
    m_msaaSamples = samples;
    save();
}

int SettingsManager::poseAB() const
{
    auto o = loadModelConfigObject(selectedModelFolder());
    int v = o.value("poseAB").toInt(0);
    if (v != 0 && v != 1) v = 0;
    return v;
}

void SettingsManager::setPoseAB(int index)
{
    if (index != 0 && index != 1) index = 0;
    auto f = selectedModelFolder();
    auto o = loadModelConfigObject(f);
    o["poseAB"] = index;
    saveModelConfigObject(f, o);
}

QString SettingsManager::aiBaseUrl() const { return m_aiBaseUrl; }
void SettingsManager::setAiBaseUrl(const QString& url) { m_aiBaseUrl = url.trimmed(); save(); }
QString SettingsManager::aiApiKey() const { return m_aiApiKey; }
void SettingsManager::setAiApiKey(const QString& key) { m_aiApiKey = key; save(); }
QString SettingsManager::aiModel() const { return m_aiModel; }
void SettingsManager::setAiModel(const QString& model) { m_aiModel = model.trimmed(); save(); }
QString SettingsManager::aiSystemPrompt() const { return m_aiSystemPrompt; }
void SettingsManager::setAiSystemPrompt(const QString& prompt) { m_aiSystemPrompt = prompt; save(); }
bool SettingsManager::aiStreamEnabled() const { return m_aiStream; }
void SettingsManager::setAiStreamEnabled(bool enabled) { m_aiStream = enabled; save(); }

// MCP servers (from mcp.json)
QList<McpServerConfig> SettingsManager::mcpServers() const
{
    return m_mcpServers;
}

void SettingsManager::setMcpServers(const QList<McpServerConfig>& servers)
{
    m_mcpServers = servers;
    m_mcpEnabledStates.clear();
    for (const McpServerConfig& server : m_mcpServers)
        m_mcpEnabledStates.insert(server.name, server.enabled);
    saveMcpServersToFile();
    save();
}

void SettingsManager::addMcpServer(const McpServerConfig& server)
{
    // Remove any existing server with the same name
    m_mcpServers.erase(std::remove_if(m_mcpServers.begin(), m_mcpServers.end(),
        [&server](const McpServerConfig& s) { return s.name == server.name; }), m_mcpServers.end());
    m_mcpServers.append(server);
    m_mcpEnabledStates.insert(server.name, server.enabled);
    saveMcpServersToFile();
    save();
}

void SettingsManager::removeMcpServer(const QString& serverName)
{
    m_mcpServers.erase(std::remove_if(m_mcpServers.begin(), m_mcpServers.end(),
        [&serverName](const McpServerConfig& s) { return s.name == serverName; }), m_mcpServers.end());
    m_mcpEnabledStates.remove(serverName);
    saveMcpServersToFile();
    save();
}

void SettingsManager::updateMcpServer(const McpServerConfig& server)
{
    for (auto& s : m_mcpServers) {
        if (s.name == server.name) {
            s = server;
            m_mcpEnabledStates.insert(server.name, server.enabled);
            saveMcpServersToFile();
            save();
            return;
        }
    }
}

bool SettingsManager::hasMcpServer(const QString& serverName) const
{
    for (const auto& s : m_mcpServers) {
        if (s.name == serverName)
            return true;
    }
    return false;
}

McpServerConfig SettingsManager::mcpServer(const QString& serverName) const
{
    for (const auto& s : m_mcpServers) {
        if (s.name == serverName)
            return s;
    }
    return McpServerConfig();
}

QString SettingsManager::mcpJsonPath() const
{
    return QDir(configDir()).filePath(QStringLiteral("mcp.json"));
}

void SettingsManager::loadMcpServersFromFile()
{
    QFile f(mcpJsonPath());
    if (!f.exists()) {
        m_mcpServers.clear();
        return;
    }

    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open mcp.json:" << f.errorString();
        return;
    }

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse mcp.json:" << err.errorString();
        return;
    }

    m_mcpServers.clear();

    if (doc.isObject())
    {
        const QJsonObject root = doc.object();

        // Preferred format: { "mcpServers": { "name": { ... } } }
        if (root.value(QStringLiteral("mcpServers")).isObject())
        {
            const QJsonObject serversObj = root.value(QStringLiteral("mcpServers")).toObject();
            for (auto it = serversObj.begin(); it != serversObj.end(); ++it)
            {
                if (!it.value().isObject())
                    continue;
                McpServerConfig config = McpServerConfig::fromJson(it.value().toObject());
                config.name = it.key();
                config.enabled = m_mcpEnabledStates.value(config.name, true);
                if (!config.name.trimmed().isEmpty())
                    m_mcpServers.append(config);
            }
            return;
        }

        // Backward compatibility: { "version": 1, "servers": [ ... ] }
        if (root.value(QStringLiteral("servers")).isArray())
        {
            const QJsonArray serversArr = root.value(QStringLiteral("servers")).toArray();
            for (const QJsonValue& val : serversArr)
            {
                if (!val.isObject())
                    continue;
                McpServerConfig config = McpServerConfig::fromJson(val.toObject());
                config.enabled = m_mcpEnabledStates.value(config.name, true);
                if (!config.name.trimmed().isEmpty())
                    m_mcpServers.append(config);
            }
            return;
        }
    }

    // Backward compatibility: old format was a plain array.
    if (doc.isArray())
    {
        const QJsonArray serversArr = doc.array();
        for (const QJsonValue& val : serversArr)
        {
            if (!val.isObject())
                continue;
            McpServerConfig config = McpServerConfig::fromJson(val.toObject());
            config.enabled = m_mcpEnabledStates.value(config.name, true);
            if (!config.name.trimmed().isEmpty())
                m_mcpServers.append(config);
        }
    }
}

void SettingsManager::saveMcpServersToFile() const
{
    QDir dir(configDir());
    if (!dir.exists()) dir.mkpath(".");

    QJsonObject mcpServersObj;
    for (const auto& server : m_mcpServers) {
        if (!server.name.trimmed().isEmpty())
            mcpServersObj.insert(server.name, server.toJson());
    }

    QJsonObject root;
    root.insert(QStringLiteral("mcpServers"), mcpServersObj);

    QFile f(mcpJsonPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    } else {
        qWarning() << "Cannot write mcp.json:" << f.errorString();
    }
}
