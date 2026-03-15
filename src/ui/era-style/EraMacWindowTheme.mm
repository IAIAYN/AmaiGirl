#include "ui/era-style/EraNativeWindowTheme.hpp"

#import <AppKit/AppKit.h>

#include <QWidget>

#include "ui/era-style/EraStyleColor.hpp"

namespace
{
NSColor* toNsColor(const QColor& color)
{
    return [NSColor colorWithSRGBRed:color.redF()
                               green:color.greenF()
                                blue:color.blueF()
                               alpha:color.alphaF()];
}

NSWindow* nativeWindowFor(QWidget* widget)
{
    if (!widget)
        return nil;

    QWidget* topLevel = widget->window();
    if (!topLevel)
        topLevel = widget;

    if (!topLevel->testAttribute(Qt::WA_WState_Created))
        topLevel->winId();

    NSView* view = reinterpret_cast<NSView*>(topLevel->winId());
    if (!view)
        return nil;

    return view.window;
}
}

namespace EraStyle
{
void syncNativeWindowTheme(QWidget* widget)
{
    NSWindow* window = nativeWindowFor(widget);
    if (!window)
        return;

    const EraStyleColor::ThemePalette& palette = EraStyleColor::themePalette();

    window.appearance = EraStyleColor::isDark()
        ? [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]
        : [NSAppearance appearanceNamed:NSAppearanceNameAqua];

    window.backgroundColor = toNsColor(palette.windowBackground);
    window.titlebarAppearsTransparent = YES;

    if (@available(macOS 11.0, *))
        window.toolbarStyle = NSWindowToolbarStyleUnified;
}
}