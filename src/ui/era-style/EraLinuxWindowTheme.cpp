#include "ui/era-style/EraMacWindowTheme.hpp"

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)

#include <QWidget>

namespace EraStyle
{
void syncNativeWindowTheme(QWidget* widget)
{
    // Most Linux desktop environments draw server-side title bars,
    // and there is no universal API to force per-window caption colors.
    // Keep a no-op implementation so theme refresh flow stays consistent.
    Q_UNUSED(widget);
}
}  // namespace EraStyle

#endif
