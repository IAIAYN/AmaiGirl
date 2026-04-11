#pragma once

#include <QString>

class IToolRegistry
{
public:
    virtual ~IToolRegistry() = default;

    virtual void clear() = 0;
    virtual void registerRoute(const QString& exposedToolName,
                               const QString& serverName,
                               const QString& rawToolName) = 0;
    virtual bool resolveRoute(const QString& exposedToolName,
                              QString* serverName,
                              QString* rawToolName) const = 0;
};
