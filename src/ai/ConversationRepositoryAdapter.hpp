#pragma once

#include "ai/core/IConversationStore.hpp"

class ConversationRepositoryAdapter final : public IConversationStore
{
public:
    QJsonArray loadMessages(const QString& modelFolder) const override;
    void saveMessages(const QString& modelFolder, const QJsonArray& messages) override;
    QString latestAssistantMessage(const QString& modelFolder) const override;
};