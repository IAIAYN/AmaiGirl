#pragma once

#include <QList>
#include <QMetaType>
#include <QString>

enum class McpServerRuntimeState
{
    Disabled,
    Starting,
    Enabled,
    Unavailable,
};

struct McpServerStatus
{
    QString name;
    bool enabled{false};
    McpServerRuntimeState state{McpServerRuntimeState::Disabled};
    QString detail;
};

Q_DECLARE_METATYPE(McpServerStatus)
Q_DECLARE_METATYPE(QList<McpServerStatus>)