#pragma once

#include <QJsonArray>
#include <QString>

class IConversationStore
{
public:
    virtual ~IConversationStore() = default;

    virtual QJsonArray loadMessages(const QString& modelFolder) const = 0;
    virtual void saveMessages(const QString& modelFolder, const QJsonArray& messages) = 0;
    virtual QString latestAssistantMessage(const QString& modelFolder) const = 0;
};
