#pragma once

#include <QString>
#include <QStringList>

class QApplication;
class QAbstractScrollArea;

namespace EraStyle
{
    QString normalizeThemeId(const QString& themeId);
    QStringList availableThemeIds();
    void installApplicationStyle(QApplication& app, const QString& themeId = QString());
    void applyTheme(QApplication& app, const QString& themeId = QString());
    void installHoverScrollBars(QAbstractScrollArea* area, bool enableVertical = true, bool enableHorizontal = true);
}  // namespace EraStyle
