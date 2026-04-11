#pragma once

#include <QString>

class ITtsProvider
{
public:
    struct TtsConfig
    {
        QString baseUrl;
        QString apiKey;
        QString model;
        QString voice;
    };

    virtual ~ITtsProvider() = default;

    virtual void setConfig(TtsConfig cfg) = 0;
    virtual TtsConfig config() const = 0;
    virtual bool isBusy() const = 0;
    virtual void startSpeech(const QString& text, const QString& outPath) = 0;
    virtual void cancel() = 0;
};
