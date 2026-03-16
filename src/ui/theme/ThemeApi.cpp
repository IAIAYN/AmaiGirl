#include "ui/theme/ThemeApi.hpp"

#include "common/SettingsManager.hpp"
#include "common/Utils.hpp"
#include "ui/era-style/EraStyleHelper.hpp"

namespace
{
QString resolveThemeId(const QString& requestedThemeId)
{
    if (!requestedThemeId.trimmed().isEmpty())
        return EraStyle::normalizeThemeId(requestedThemeId);

    return EraStyle::normalizeThemeId(SettingsManager::instance().theme());
}

QString eraIconRelativePath(Theme::IconToken token)
{
    switch (token)
    {
    case Theme::IconToken::ChatSend:
        return QStringLiteral("icons/era-style/chat-send.svg");
    case Theme::IconToken::ChatClear:
        return QStringLiteral("icons/era-style/chat-clear.svg");
    case Theme::IconToken::SettingsBasic:
        return QStringLiteral("icons/era-style/settings-basic.svg");
    case Theme::IconToken::SettingsModel:
        return QStringLiteral("icons/era-style/settings-model.svg");
    case Theme::IconToken::SettingsAi:
        return QStringLiteral("icons/era-style/settings-ai.svg");
    case Theme::IconToken::SettingsAdvanced:
        return QStringLiteral("icons/era-style/settings-advanced.svg");
    case Theme::IconToken::ThemeLightIndicator:
        return QStringLiteral("icons/era-style/theme-light.svg");
    case Theme::IconToken::ThemeDarkIndicator:
        return QStringLiteral("icons/era-style/theme-dark.svg");
    }

    return QStringLiteral("icons/era-style/chat-send.svg");
}
}  // namespace

namespace Theme
{
QString normalizeThemeId(const QString& themeId)
{
    return EraStyle::normalizeThemeId(themeId);
}

QStringList availableThemeIds()
{
    return EraStyle::availableThemeIds();
}

void installApplicationStyle(QApplication& app, const QString& themeId)
{
    EraStyle::installApplicationStyle(app, resolveThemeId(themeId));
}

void applyTheme(QApplication& app, const QString& themeId)
{
    EraStyle::applyTheme(app, resolveThemeId(themeId));
}

QString iconRelativePath(IconToken token, const QString& themeId)
{
    const QString resolvedTheme = resolveThemeId(themeId);
    if (resolvedTheme == QStringLiteral("era"))
        return eraIconRelativePath(token);

    return eraIconRelativePath(token);
}

QIcon themedIcon(IconToken token, const QString& themeId)
{
    return QIcon(appResourcePath(iconRelativePath(token, themeId)));
}
}  // namespace Theme
