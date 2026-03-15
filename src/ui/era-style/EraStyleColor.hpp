#pragma once

#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

namespace EraStyleColor
{
struct ThemePalette
{
    QColor windowBackground;
    QColor panelBackground;
    QColor panelRaised;
    QColor inputBackground;
    QColor inputBackgroundDisabled;
    QColor popupBackground;

    QColor textPrimary;
    QColor textSecondary;
    QColor textMuted;
    QColor textDisabled;

    QColor borderPrimary;
    QColor borderSecondary;
    QColor divider;

    QColor accent;
    QColor accentHover;
    QColor accentPressed;

    QColor success;
    QColor successHover;
    QColor successPressed;

    QColor warning;
    QColor warningHover;
    QColor warningPressed;

    QColor danger;
    QColor dangerHover;
    QColor dangerPressed;

    QColor info;
    QColor infoHover;
    QColor infoPressed;

    QColor onAccentText;
    QColor selectionBackground;
    QColor selectionText;
    QColor hoverBackground;

    QColor tooltipBackground;
    QColor tooltipBorder;
    QColor tooltipText;

    QColor scrollbarHandle;
    QColor scrollbarHandleHover;
    QColor scrollbarHandlePressed;

    QColor tabDivider;
    QColor tabTextIdle;
    QColor tabTextHover;
    QColor tabTextActive;
    QColor tabIndicator;
    QColor tabHoverBackground;
    QColor tabActiveBackground;
};

inline bool isDark()
{
    if (!QGuiApplication::instance())
        return false;

    if (const QStyleHints* hints = QGuiApplication::styleHints())
    {
        const Qt::ColorScheme scheme = hints->colorScheme();
        if (scheme == Qt::ColorScheme::Dark)
            return true;
        if (scheme == Qt::ColorScheme::Light)
            return false;
    }

    const QPalette pal = QGuiApplication::palette();
    const QColor w = pal.color(QPalette::Window);
    const QColor b = pal.color(QPalette::Base);
    return ((w.lightnessF() + b.lightnessF()) * 0.5) < 0.5;
}

inline const ThemePalette& themePalette()
{
    static const ThemePalette kLight = {
        QColor(0xf5, 0xf7, 0xfb), // windowBackground
        QColor(0xff, 0xff, 0xff), // panelBackground
        QColor(0xfb, 0xfd, 0xff), // panelRaised
        QColor(0xff, 0xff, 0xff), // inputBackground
        QColor(0xec, 0xf0, 0xf6), // inputBackgroundDisabled
        QColor(0xff, 0xff, 0xff), // popupBackground

        QColor(0x1f, 0x24, 0x30), // textPrimary
        QColor(0x4a, 0x57, 0x6b), // textSecondary
        QColor(0x7e, 0x8a, 0x9b), // textMuted
        QColor(0xac, 0xb5, 0xc3), // textDisabled

        QColor(0xd2, 0xdb, 0xe8), // borderPrimary
        QColor(0xe6, 0xec, 0xf4), // borderSecondary
        QColor(0xd4, 0xdd, 0xea), // divider

        QColor(0x2f, 0x73, 0xff), // accent
        QColor(0x5a, 0x92, 0xff), // accentHover
        QColor(0x24, 0x63, 0xe0), // accentPressed

        QColor(0x2e, 0xb8, 0x62), // success
        QColor(0x48, 0xc7, 0x74), // successHover
        QColor(0x25, 0x97, 0x50), // successPressed

        QColor(0xf3, 0x9a, 0x1e), // warning
        QColor(0xff, 0xb0, 0x45), // warningHover
        QColor(0xd5, 0x83, 0x12), // warningPressed

        QColor(0xf1, 0x4f, 0x45), // danger
        QColor(0xf6, 0x74, 0x67), // dangerHover
        QColor(0xce, 0x3f, 0x38), // dangerPressed

        QColor(0x1a, 0xaa, 0xf7), // info
        QColor(0x51, 0xbf, 0xff), // infoHover
        QColor(0x14, 0x8f, 0xd2), // infoPressed

        QColor(0xff, 0xff, 0xff), // onAccentText
        QColor(0xe2, 0xec, 0xff), // selectionBackground
        QColor(0x1a, 0x2b, 0x53), // selectionText
        QColor(0xeb, 0xf2, 0xff), // hoverBackground

        QColor(0xff, 0xff, 0xff), // tooltipBackground
        QColor(0xc8, 0xd4, 0xe6), // tooltipBorder
        QColor(0x1f, 0x24, 0x30), // tooltipText

        QColor(0x8d, 0x99, 0xad), // scrollbarHandle
        QColor(0x6e, 0x8a, 0xb8), // scrollbarHandleHover
        QColor(0x5b, 0x76, 0xa2), // scrollbarHandlePressed

        QColor(0xd4, 0xdd, 0xea), // tabDivider
        QColor(0x7a, 0x86, 0x98), // tabTextIdle
        QColor(0x3f, 0x4d, 0x62), // tabTextHover
        QColor(0x2f, 0x73, 0xff), // tabTextActive
        QColor(0x2f, 0x73, 0xff), // tabIndicator
        QColor(0xe9, 0xf1, 0xff), // tabHoverBackground
        QColor(0xde, 0xe9, 0xff)  // tabActiveBackground
    };

    static const ThemePalette kDark = {
        QColor(0x12, 0x17, 0x21), // windowBackground
        QColor(0x18, 0x1f, 0x2b), // panelBackground
        QColor(0x1f, 0x27, 0x34), // panelRaised
        QColor(0x1d, 0x26, 0x33), // inputBackground
        QColor(0x24, 0x2d, 0x3a), // inputBackgroundDisabled
        QColor(0x1d, 0x26, 0x34), // popupBackground

        QColor(0xe7, 0xed, 0xf7), // textPrimary
        QColor(0xbd, 0xc7, 0xd9), // textSecondary
        QColor(0x8a, 0x96, 0xaa), // textMuted
        QColor(0x67, 0x72, 0x86), // textDisabled

        QColor(0x36, 0x43, 0x59), // borderPrimary
        QColor(0x2a, 0x35, 0x48), // borderSecondary
        QColor(0x3a, 0x47, 0x60), // divider

        QColor(0x76, 0xa9, 0xff), // accent
        QColor(0x8b, 0xb8, 0xff), // accentHover
        QColor(0x5d, 0x95, 0xf2), // accentPressed

        QColor(0x45, 0xc9, 0x7a), // success
        QColor(0x62, 0xd6, 0x8f), // successHover
        QColor(0x35, 0xac, 0x68), // successPressed

        QColor(0xff, 0xb1, 0x4a), // warning
        QColor(0xff, 0xc2, 0x6a), // warningHover
        QColor(0xe4, 0x99, 0x36), // warningPressed

        QColor(0xff, 0x7a, 0x6f), // danger
        QColor(0xff, 0x95, 0x8b), // dangerHover
        QColor(0xe3, 0x61, 0x56), // dangerPressed

        QColor(0x53, 0xc5, 0xff), // info
        QColor(0x77, 0xd1, 0xff), // infoHover
        QColor(0x3b, 0xaa, 0xe0), // infoPressed

        QColor(0xff, 0xff, 0xff), // onAccentText
        QColor(0x2f, 0x45, 0x69), // selectionBackground
        QColor(0xec, 0xf2, 0xff), // selectionText
        QColor(0x2a, 0x35, 0x49), // hoverBackground

        QColor(0x26, 0x31, 0x44), // tooltipBackground
        QColor(0x3d, 0x4b, 0x63), // tooltipBorder
        QColor(0xe7, 0xed, 0xf7), // tooltipText

        QColor(0x8e, 0x9c, 0xb3), // scrollbarHandle
        QColor(0xa1, 0xb1, 0xc9), // scrollbarHandleHover
        QColor(0x88, 0x9d, 0xbd), // scrollbarHandlePressed

        QColor(0x3a, 0x47, 0x60), // tabDivider
        QColor(0x8e, 0x99, 0xab), // tabTextIdle
        QColor(0xc9, 0xd2, 0xe3), // tabTextHover
        QColor(0x86, 0xb8, 0xff), // tabTextActive
        QColor(0x64, 0xa6, 0xff), // tabIndicator
        QColor(0x2a, 0x34, 0x48, 0x90), // tabHoverBackground
        QColor(0x31, 0x3f, 0x5a, 0xb0)  // tabActiveBackground
    };

    return isDark() ? kDark : kLight;
}

inline QPalette applicationPalette()
{
    const ThemePalette& t = themePalette();
    QPalette p;

    p.setColor(QPalette::Window, t.windowBackground);
    p.setColor(QPalette::WindowText, t.textPrimary);
    p.setColor(QPalette::Base, t.inputBackground);
    p.setColor(QPalette::AlternateBase, t.panelRaised);
    p.setColor(QPalette::ToolTipBase, t.tooltipBackground);
    p.setColor(QPalette::ToolTipText, t.tooltipText);
    p.setColor(QPalette::Text, t.textPrimary);
    p.setColor(QPalette::PlaceholderText, t.textMuted);
    p.setColor(QPalette::Button, t.panelBackground);
    p.setColor(QPalette::ButtonText, t.textPrimary);
    p.setColor(QPalette::BrightText, t.onAccentText);
    p.setColor(QPalette::Link, t.accent);
    p.setColor(QPalette::Highlight, t.selectionBackground);
    p.setColor(QPalette::HighlightedText, t.selectionText);

    p.setColor(QPalette::Disabled, QPalette::WindowText, t.textDisabled);
    p.setColor(QPalette::Disabled, QPalette::Text, t.textDisabled);
    p.setColor(QPalette::Disabled, QPalette::PlaceholderText, t.textDisabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, t.textDisabled);
    p.setColor(QPalette::Disabled, QPalette::Button, t.inputBackgroundDisabled);
    p.setColor(QPalette::Disabled, QPalette::Base, t.inputBackgroundDisabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, t.borderPrimary);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, t.textSecondary);
    return p;
}

// Legacy tokens kept for compatibility with existing widgets.
inline const QColor DarkSurface{0x1d, 0x26, 0x33, 0xff};
inline const QColor DarkSurfaceSubtle{0x24, 0x2d, 0x3a, 0xff};
inline const QColor DarkPopup{0x1d, 0x26, 0x34, 0xff};
inline const QColor DarkMainText{0xe7, 0xed, 0xf7, 0xff};
inline const QColor DarkSubordinateText{0xbd, 0xc7, 0xd9, 0xff};
inline const QColor DarkAuxiliaryText{0x8a, 0x96, 0xaa, 0xff};
inline const QColor DarkDisabledText{0x67, 0x72, 0x86, 0xff};
inline const QColor DarkPrimaryBorder{0x36, 0x43, 0x59, 0xff};
inline const QColor DarkSecondaryBorder{0x2a, 0x35, 0x48, 0xff};
inline const QColor DarkTooltipBackground{0x26, 0x31, 0x44, 0xff};
inline const QColor DarkHoverFill{0x2a, 0x35, 0x49, 0xff};

inline const QColor Link{0x2f, 0x73, 0xff, 0xff};
inline const QColor LinkHover{0x5a, 0x92, 0xff, 0xff};
inline const QColor LinkClick{0x24, 0x63, 0xe0, 0xff};

inline const QColor Success{0x2e, 0xb8, 0x62, 0xff};
inline const QColor SuccessHover{0x48, 0xc7, 0x74, 0xff};
inline const QColor SuccessClick{0x25, 0x97, 0x50, 0xff};

inline const QColor Warning{0xf3, 0x9a, 0x1e, 0xff};
inline const QColor WarningHover{0xff, 0xb0, 0x45, 0xff};
inline const QColor WarningClick{0xd5, 0x83, 0x12, 0xff};

inline const QColor Danger{0xf1, 0x4f, 0x45, 0xff};
inline const QColor DangerHover{0xf6, 0x74, 0x67, 0xff};
inline const QColor DangerClick{0xce, 0x3f, 0x38, 0xff};

inline const QColor Info{0x1a, 0xaa, 0xf7, 0xff};
inline const QColor InfoHover{0x51, 0xbf, 0xff, 0xff};
inline const QColor InfoClick{0x14, 0x8f, 0xd2, 0xff};

inline const QColor BasicGray{0xec, 0xf0, 0xf6, 0xff};
inline const QColor BasicWhite{0xff, 0xff, 0xff, 0xff};

inline const QColor MainText{0x1f, 0x24, 0x30, 0xff};
inline const QColor SubordinateText{0x4a, 0x57, 0x6b, 0xff};
inline const QColor AuxiliaryText{0x7e, 0x8a, 0x9b, 0xff};
inline const QColor DisabledText{0xac, 0xb5, 0xc3, 0xff};

inline const QColor PrimaryBorder{0xd2, 0xdb, 0xe8, 0xff};
inline const QColor SecondaryBorder{0xe6, 0xec, 0xf4, 0xff};
inline const QColor ThreeLevels{0xe6, 0xec, 0xf4, 0xff};
}  // namespace EraStyleColor
