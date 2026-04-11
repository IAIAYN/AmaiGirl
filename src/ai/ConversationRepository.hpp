#pragma once

#include <QJsonArray>
#include <QString>

class ConversationRepository
{
public:
    static QJsonArray loadMessages(const QString& modelFolder);
    static void saveMessages(const QString& modelFolder, const QJsonArray& messages);
    static QString latestAssistantMessage(const QString& modelFolder);
};
