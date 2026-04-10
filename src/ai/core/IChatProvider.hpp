#pragma once

#include <QByteArray>
#include <QString>

class IChatProvider
{
public:
    struct ChatConfig
    {
        QString baseUrl;
        QString apiKey;
        QString model;
        QString systemPrompt;
        bool stream{true};
    };

    virtual ~IChatProvider() = default;

    virtual void setConfig(ChatConfig cfg) = 0;
    virtual ChatConfig config() const = 0;
    virtual bool isBusy() const = 0;
    virtual void startChat(const QByteArray& messagesJson) = 0;
    virtual void cancel() = 0;
};