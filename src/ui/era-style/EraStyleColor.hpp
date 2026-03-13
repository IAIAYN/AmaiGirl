#pragma once

#include <QColor>
#include <QGuiApplication>
#include <QStyleHints>

namespace EraStyleColor
{
    // ---------- Dark-mode semantic colors ----------
    // Surface / input background in dark mode
    inline const QColor DarkSurface{0x1e, 0x1f, 0x20, 0xff};
    // Slightly elevated surface (disabled, inactive)
    inline const QColor DarkSurfaceSubtle{0x2a, 0x2b, 0x2d, 0xff};
    // Popups / dropdown background
    inline const QColor DarkPopup{0x28, 0x29, 0x2b, 0xff};
    // Main text in dark mode
    inline const QColor DarkMainText{0xe6, 0xe7, 0xe8, 0xff};
    // Subordinate text in dark mode
    inline const QColor DarkSubordinateText{0xb0, 0xb1, 0xb3, 0xff};
    // Auxiliary / placeholder text in dark mode
    inline const QColor DarkAuxiliaryText{0x74, 0x75, 0x78, 0xff};
    // Disabled text in dark mode
    inline const QColor DarkDisabledText{0x52, 0x53, 0x55, 0xff};
    // Primary border in dark mode
    inline const QColor DarkPrimaryBorder{0x3e, 0x3f, 0x42, 0xff};
    // Secondary border / separator
    inline const QColor DarkSecondaryBorder{0x31, 0x32, 0x35, 0xff};
    // Tooltip background in dark mode
    inline const QColor DarkTooltipBackground{0x3a, 0x3b, 0x3e, 0xff};
    // Hover fill in dark mode
    inline const QColor DarkHoverFill{0x35, 0x36, 0x3a, 0xff};

    // ---------- Utility ----------
    inline bool isDark()
    {
        if (!QGuiApplication::instance())
            return false;
        const QStyleHints* hints = QGuiApplication::styleHints();
        if (!hints)
            return false;
        return hints->colorScheme() == Qt::ColorScheme::Dark;
    }


    inline const QColor Link{0x32, 0x7d, 0xff, 0xff};
    inline const QColor LinkHover{0x59, 0x90, 0xff, 0xff};
    inline const QColor LinkClick{0x2a, 0x70, 0xd9, 0xff};

    inline const QColor Success{0x31, 0xbf, 0x30, 0xff};
    inline const QColor SuccessHover{0x56, 0xd6, 0x5a, 0xff};
    inline const QColor SuccessClick{0x2e, 0xa6, 0x2a, 0xff};

    inline const QColor Warning{0xff, 0xa1, 0x14, 0xff};
    inline const QColor WarningHover{0xff, 0xbb, 0x45, 0xff};
    inline const QColor WarningClick{0xd9, 0x82, 0x11, 0xff};

    inline const QColor Danger{0xff, 0x49, 0x40, 0xff};
    inline const QColor DangerHover{0xff, 0x73, 0x66, 0xff};
    inline const QColor DangerClick{0xd9, 0x39, 0x36, 0xff};

    inline const QColor Info{0x19, 0xb2, 0xff, 0xff};
    inline const QColor InfoHover{0x61, 0xc5, 0xff, 0xff};
    inline const QColor InfoClick{0x15, 0x9e, 0xd9, 0xff};

    inline const QColor BasicGray{0xf0, 0xf2, 0xf5, 0xff};
    inline const QColor BasicWhite{0xff, 0xff, 0xff, 0xff};

    inline const QColor MainText{0x25, 0x26, 0x26, 0xff};
    inline const QColor SubordinateText{0x57, 0x58, 0x59, 0xff};
    inline const QColor AuxiliaryText{0x89, 0x8a, 0x8c, 0xff};
    inline const QColor DisabledText{0xbb, 0xbd, 0xbf, 0xff};

    inline const QColor PrimaryBorder{0xd5, 0xd6, 0xd9, 0xff};
    inline const QColor SecondaryBorder{0xeb, 0xed, 0xf0, 0xff};
    inline const QColor ThreeLevels{0xeb, 0xed, 0xf0, 0xff};
}  // namespace EraStyleColor
