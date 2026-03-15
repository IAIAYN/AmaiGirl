#pragma once

class QApplication;
class QAbstractScrollArea;

namespace EraStyle
{
    void installApplicationStyle(QApplication& app);
    void installHoverScrollBars(QAbstractScrollArea* area, bool enableVertical = true, bool enableHorizontal = true);
}  // namespace EraStyle
