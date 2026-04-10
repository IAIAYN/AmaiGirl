#include "ai/ConversationRepositoryAdapter.hpp"

#include "ai/ConversationRepository.hpp"

QJsonArray ConversationRepositoryAdapter::loadMessages(const QString& modelFolder) const
{
    return ConversationRepository::loadMessages(modelFolder);
}

void ConversationRepositoryAdapter::saveMessages(const QString& modelFolder, const QJsonArray& messages)
{
    ConversationRepository::saveMessages(modelFolder, messages);
}

QString ConversationRepositoryAdapter::latestAssistantMessage(const QString& modelFolder) const
{
    return ConversationRepository::latestAssistantMessage(modelFolder);
}