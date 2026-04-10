#include "ai/core/ToolRegistry.hpp"

void ToolRegistry::clear()
{
    m_routes.clear();
}

void ToolRegistry::registerRoute(const QString& exposedToolName,
                                 const QString& serverName,
                                 const QString& rawToolName)
{
    if (exposedToolName.trimmed().isEmpty())
        return;
    if (serverName.trimmed().isEmpty() || rawToolName.trimmed().isEmpty())
        return;

    m_routes.insert(exposedToolName, qMakePair(serverName, rawToolName));
}

bool ToolRegistry::resolveRoute(const QString& exposedToolName,
                                QString* serverName,
                                QString* rawToolName) const
{
    const auto it = m_routes.constFind(exposedToolName);
    if (it == m_routes.constEnd())
        return false;

    if (serverName)
        *serverName = it.value().first;
    if (rawToolName)
        *rawToolName = it.value().second;
    return true;
}
