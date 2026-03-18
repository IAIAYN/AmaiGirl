#pragma once

#include <QIcon>
#include <QString>
#include <QStringList>

class QApplication;

namespace Theme
{
enum class IconToken
{
    ChatSend,
    ChatClear,
    SettingsBasic,
    SettingsModel,
    SettingsAi,
    SettingsAdvanced,
    ThemeLightIndicator,
    ThemeDarkIndicator,
};

QString normalizeThemeId(const QString& themeId);
QStringList availableThemeIds();

void installApplicationStyle(QApplication& app, const QString& themeId = QString());
void applyTheme(QApplication& app, const QString& themeId = QString());

QString iconRelativePath(IconToken token, const QString& themeId = QString());
QIcon themedIcon(IconToken token, const QString& themeId = QString());
}  // namespace Theme
