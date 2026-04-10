#pragma once

#include "ai/core/IToolRegistry.hpp"

#include <QHash>
#include <QPair>

class ToolRegistry final : public IToolRegistry
{
public:
    void clear() override;
    void registerRoute(const QString& exposedToolName,
                       const QString& serverName,
                       const QString& rawToolName) override;
    bool resolveRoute(const QString& exposedToolName,
                      QString* serverName,
                      QString* rawToolName) const override;

private:
    QHash<QString, QPair<QString, QString>> m_routes;
};
