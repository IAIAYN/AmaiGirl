#include "ui/era-style/EraNativeWindowTheme.hpp"

#include <QtCore/qsystemdetection.h>

#if defined(Q_OS_WIN32)

#include <QWidget>

#include <dwmapi.h>
#include <windows.h>

#include "ui/era-style/EraStyleColor.hpp"

namespace {
// Windows 10/11: dark title bar flag. Supported on modern DWM.
constexpr DWORD kDwmUseImmersiveDarkMode = 20;
// Windows 11: caption color.
constexpr DWORD kDwmCaptionColor = 35;
// Windows 11: title text color.
constexpr DWORD kDwmTextColor = 36;

COLORREF toColorRef(const QColor& color)
{
    return RGB(color.red(), color.green(), color.blue());
}

}  // namespace

namespace EraStyle
{
void syncNativeWindowTheme(QWidget* widget)
{
    if (!widget)
        return;

    QWidget* topLevel = widget->window();
    if (!topLevel)
        topLevel = widget;

    if (!topLevel->testAttribute(Qt::WA_WState_Created))
        topLevel->winId();

    const HWND hwnd = reinterpret_cast<HWND>(topLevel->winId());
    if (!hwnd)
        return;

    const bool dark = EraStyleColor::isDark();
    const EraStyleColor::ThemePalette& pal = EraStyleColor::themePalette();

    const BOOL useDark = dark ? TRUE : FALSE;
    (void)DwmSetWindowAttribute(hwnd, kDwmUseImmersiveDarkMode, &useDark, sizeof(useDark));

    const COLORREF captionColor = toColorRef(pal.windowBackground);
    (void)DwmSetWindowAttribute(hwnd, kDwmCaptionColor, &captionColor, sizeof(captionColor));

    const COLORREF textColor = toColorRef(pal.textPrimary);
    (void)DwmSetWindowAttribute(hwnd, kDwmTextColor, &textColor, sizeof(textColor));
}
}  // namespace EraStyle

#endif
