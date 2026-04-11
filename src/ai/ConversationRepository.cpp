#include "ai/ConversationRepository.hpp"

#include "common/SettingsManager.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

QJsonArray ConversationRepository::loadMessages(const QString& modelFolder)
{
    QFile f(SettingsManager::instance().chatPathForModel(modelFolder));
    if (!f.exists()) return {};
    if (!f.open(QIODevice::ReadOnly)) return {};

    QJsonParseError e;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return doc.object().value("messages").toArray();
}

void ConversationRepository::saveMessages(const QString& modelFolder, const QJsonArray& messages)
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

QString ConversationRepository::latestAssistantMessage(const QString& modelFolder)
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
