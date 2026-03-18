#include "ui/era-style/EraStyleHelper.hpp"
#include "ui/era-style/EraStyleColor.hpp"

#include <algorithm>
#include <cmath>
#include <QAbstractAnimation>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QEasingCurve>
#include <QEvent>
#include <QObject>
#include <QPainter>
#include <QPaintEvent>
#include <QPointer>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTimer>
#include <QVariantAnimation>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QList>
#include <QPen>
#include <QScreen>
#include <QLayout>
#include <QWidget>

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN32) || defined(Q_OS_LINUX)
#include "ui/era-style/EraNativeWindowTheme.hpp"
#endif

namespace {
constexpr auto kAppStyleInstalledProperty = "_amaigirl_era_app_style_installed";
constexpr auto kCurrentThemeIdProperty = "_amaigirl_era_theme_id";
constexpr auto kThemeSyncInstalledProperty = "_amaigirl_era_theme_sync_installed";
constexpr auto kScrollBarHelperInstalledProperty = "_amaigirl_era_scrollbar_helper_installed";
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN32) || defined(Q_OS_LINUX)
constexpr auto kNativeWindowThemePendingProperty = "_amaigirl_native_window_theme_pending";
#endif
constexpr int kScrollBarHideDelayMs = 80;
constexpr int kScrollBarFadeMs = 140;
constexpr int kScrollBarExtent = 8;
constexpr int kScrollBarMargin = 2;
constexpr int kScrollBarMinHandle = 28;
constexpr qreal kHandleBaseAlpha = 0.360;
constexpr qreal kHandleHoverAlpha = 0.520;
constexpr qreal kHandlePressedAlpha = 0.640;
constexpr int kToolTipRadius = 8;
constexpr int kChatComposerRadius = 18;

QString normalizeThemeIdImpl(const QString& themeId)
{
    QString id = themeId.trimmed().toLower();
    if (id.isEmpty() || id == QStringLiteral("system"))
        return QStringLiteral("era");
    return id;
}

bool isThemeRelatedEventType(QEvent::Type type)
{
    return type == QEvent::ThemeChange
        || type == QEvent::StyleChange
        || type == QEvent::ApplicationPaletteChange
        || type == QEvent::PaletteChange;
}

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN32) || defined(Q_OS_LINUX)
bool isNativeWindowSyncEventType(QEvent::Type type)
{
    return type == QEvent::Show
        || type == QEvent::WinIdChange
        || type == QEvent::WindowStateChange;
}

void scheduleNativeWindowThemeSync(QApplication* app, QWidget* widget)
{
    if (!app || !widget || !widget->isWindow())
        return;

    if (widget->property(kNativeWindowThemePendingProperty).toBool())
        return;

    widget->setProperty(kNativeWindowThemePendingProperty, true);
    QPointer<QWidget> guarded(widget);
    QTimer::singleShot(0, app, [guarded] {
        if (!guarded)
            return;

        guarded->setProperty(kNativeWindowThemePendingProperty, false);
        EraStyle::syncNativeWindowTheme(guarded.data());
    });
}
#endif

QString toRgba(const QColor& color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(QString::number(color.alphaF(), 'f', 3));
}

void repolishWidgetTree(QWidget* root)
{
    if (!root)
        return;

    QList<QWidget*> stack;
    stack.push_back(root);

    while (!stack.isEmpty())
    {
        QWidget* w = stack.takeLast();
        if (!w)
            continue;

        if (QStyle* style = w->style())
        {
            style->unpolish(w);
            style->polish(w);
        }

        if (auto* layout = w->layout())
            layout->invalidate();

        const auto children = w->children();
        for (QObject* child : children)
        {
            if (auto* childWidget = qobject_cast<QWidget*>(child))
                stack.push_back(childWidget);
        }

        w->updateGeometry();
        w->update();
    }
}

QString buildApplicationStyleSheet()
{
    const EraStyleColor::ThemePalette& t = EraStyleColor::themePalette();
    const QColor composerCardBackground = EraStyleColor::isDark() ? t.panelRaised : t.panelBackground;
    const QColor composerCardBorder = EraStyleColor::isDark() ? t.borderPrimary : t.borderSecondary;
    return QStringLiteral(
        "QToolTip { background: transparent; border: none; color: transparent; }"
        "QMainWindow, QDialog, QMessageBox { background: %1; color: %2; }"
        "QWidget#chatCentral { background: %1; }"
        "QStatusBar, QToolBar { background: %3; color: %2; border: none; }"
        "QMenuBar { background: %3; color: %2; border-bottom: 1px solid %4; }"
        "QMenuBar::item { background: transparent; padding: 4px 8px; }"
        "QMenuBar::item:selected { background: %5; }"
        "QMenu { background: %6; color: %2; border: 1px solid %4; border-radius: 8px; padding: 4px; }"
        "QMenu::item { padding: 6px 18px 6px 12px; border-radius: 4px; }"
        "QMenu::item:selected { background: %5; color: %2; }"
        "QMenu::item:disabled { color: %7; background: transparent; }"
        "QMenu::indicator { width: 0px; height: 0px; }"
        "QMenu::separator { height: 1px; background: %4; margin: 4px 8px; }"
        "QLabel { color: %2; background: transparent; }"
        "QLabel a, QLabel a:focus, QLabel a:active, QLabel a:visited { text-decoration: none; outline: none; }"
        "QCheckBox, QRadioButton { color: %2; background: transparent; spacing: 6px; }"
        "QCheckBox:disabled, QRadioButton:disabled, QLabel:disabled { color: %7; }"
        "QGroupBox { background: %3; color: %2; border: 1px solid %4; border-radius: 6px; margin-top: 12px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: %8; }"
        "QAbstractItemView { background: %9; color: %2; border: 1px solid %10;"
        " selection-background-color: %11; selection-color: %12; }"
        "QHeaderView::section { background: %3; color: %2; border: none; border-bottom: 1px solid %4;"
        " border-right: 1px solid %4; padding: 4px 6px; }"
        "QPushButton { background: %3; color: %2; border: 1px solid %10; border-radius: 4px; padding: 6px 12px; }"
        "QPushButton:hover { background: %5; border-color: %13; }"
        "QPushButton:pressed { background: %14; border-color: %13; }"
        "QPushButton:disabled { background: %15; color: %7; border-color: %4; }"
        "QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QDateTimeEdit {"
        " background: %9; color: %2; border: 1px solid %10; border-radius: 4px;"
        " selection-background-color: %11; selection-color: %12; }"
        "QLineEdit:disabled, QTextEdit:disabled, QPlainTextEdit:disabled, QSpinBox:disabled,"
        " QDoubleSpinBox:disabled, QDateTimeEdit:disabled { background: %15; color: %7; border-color: %4; }"
        "QComboBox { background: %9; color: %2; border: 1px solid %10; border-radius: 4px; padding: 4px 8px; }"
        "QComboBox:disabled { background: %15; color: %7; border-color: %4; }"
        "QComboBox QAbstractItemView { background: %6; color: %2; border: 1px solid %10;"
        " selection-background-color: %11; selection-color: %12; }"
        "QTabWidget::pane { border: 1px solid %4; background: %3; }"
        "QTabBar::tab { background: %3; color: %8; border: 1px solid %4; border-bottom: none;"
        " padding: 6px 12px; margin-right: 2px; border-top-left-radius: 4px; border-top-right-radius: 4px; }"
        "QTabBar::tab:selected { background: %9; color: %2; }"
        "QTabBar::tab:hover:!selected { background: %5; color: %2; }"
        "QScrollBar { background: transparent; border: none; }"
        "QScrollBar:vertical { width: 8px; margin: 0px; }"
        "QScrollBar:horizontal { height: 8px; margin: 0px; }"
        "QScrollBar::groove:vertical { background: transparent; width: 0px; }"
        "QScrollBar::groove:horizontal { background: transparent; height: 0px; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }"
        "QScrollBar::add-line { height: 0px; width: 0px; border: none; }"
        "QScrollBar::sub-line { height: 0px; width: 0px; border: none; }"
        "QScrollBar::handle { background: transparent; }"
        "QWidget#chatComposerCard { background: %16; border: 1px solid %17; border-radius: %18px; }"
        "QLabel#chatComposerCountLabel { background: transparent; color: %8; font-size: 12px; }"
    )
        .arg(toRgba(t.windowBackground))
        .arg(toRgba(t.textPrimary))
        .arg(toRgba(t.panelBackground))
        .arg(toRgba(t.divider))
        .arg(toRgba(t.hoverBackground))
        .arg(toRgba(t.popupBackground))
        .arg(toRgba(t.textDisabled))
        .arg(toRgba(t.textSecondary))
        .arg(toRgba(t.inputBackground))
        .arg(toRgba(t.borderPrimary))
        .arg(toRgba(t.selectionBackground))
        .arg(toRgba(t.selectionText))
        .arg(toRgba(t.accent))
        .arg(toRgba(t.accentPressed))
        .arg(toRgba(t.inputBackgroundDisabled))
        .arg(toRgba(composerCardBackground))
        .arg(toRgba(composerCardBorder))
        .arg(kChatComposerRadius);
}

void applyApplicationTheme(QApplication& app)
{
    app.setPalette(EraStyleColor::applicationPalette());
    app.setStyleSheet(buildApplicationStyleSheet());

    const auto topLevels = app.topLevelWidgets();
    for (QWidget* w : topLevels)
    {
        repolishWidgetTree(w);
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN32) || defined(Q_OS_LINUX)
        scheduleNativeWindowThemeSync(&app, w);
#endif
    }
}

class EraThemeSyncFilter final : public QObject
{
public:
    explicit EraThemeSyncFilter(QApplication* app)
        : QObject(app)
        , m_app(app)
    {
        if (auto* hints = QGuiApplication::styleHints())
        {
            connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
                scheduleRefresh();
            });
        }
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_app && event && !m_refreshing && isThemeRelatedEventType(event->type()))
            scheduleRefresh();

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN32) || defined(Q_OS_LINUX)
        if (event && isNativeWindowSyncEventType(event->type()))
        {
            if (auto* widget = qobject_cast<QWidget*>(watched))
                scheduleNativeWindowThemeSync(m_app, widget);
        }
#endif

        return QObject::eventFilter(watched, event);
    }

private:
    void scheduleRefresh()
    {
        if (!m_app || m_refreshPending || m_refreshing)
            return;

        m_refreshPending = true;
        QTimer::singleShot(0, m_app, [this] {
            m_refreshPending = false;
            refreshApplicationTheme();
        });
    }

    void refreshApplicationTheme()
    {
        if (!m_app)
            return;

        m_refreshing = true;
        applyApplicationTheme(*m_app);
        m_refreshing = false;
    }

    QApplication* m_app{nullptr};
    bool m_refreshPending{false};
    bool m_refreshing{false};
};

class EraToolTipWidget final : public QWidget
{
    static constexpr int kPadH = 10;
    static constexpr int kPadV = 7;
    static constexpr int kMaxTextWidth = 400;

public:
    explicit EraToolTipWidget()
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setFocusPolicy(Qt::NoFocus);
    }

    void showTooltip(const QString& text, const QPoint& globalPos)
    {
        if (text.isEmpty()) {
            hide();
            return;
        }

        m_text = text;

        const QFontMetrics fm(font());
        const QRect textBound = fm.boundingRect(
            QRect(0, 0, kMaxTextWidth - 2 * kPadH, 0),
            Qt::AlignLeft | Qt::TextWordWrap,
            m_text
        );

        const int w = textBound.width() + 2 * kPadH + 2;
        const int h = textBound.height() + 2 * kPadV + 2;
        resize(w, h);

        QPoint pos = globalPos + QPoint(12, 12);
        if (const QScreen* screen = QGuiApplication::screenAt(globalPos)) {
            const QRect avail = screen->availableGeometry();
            if (pos.x() + w > avail.right())
                pos.setX(avail.right() - w - 4);
            if (pos.y() + h > avail.bottom())
                pos.setY(globalPos.y() - h - 4);
            pos.setX(std::max(pos.x(), avail.left()));
            pos.setY(std::max(pos.y(), avail.top()));
        }

        move(pos);
        show();
        raise();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF bgRect = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const EraStyleColor::ThemePalette& t = EraStyleColor::themePalette();
        painter.setBrush(t.tooltipBackground);
        painter.setPen(QPen(t.tooltipBorder, 1.0));
        painter.drawRoundedRect(bgRect, kToolTipRadius, kToolTipRadius);

        painter.setPen(t.tooltipText);
        const QRect textRect = rect().adjusted(kPadH, kPadV, -kPadH, -kPadV);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_text);
    }

private:
    QString m_text;
};

class EraToolTipFilter final : public QObject
{
public:
    explicit EraToolTipFilter(QObject* parent = nullptr)
        : QObject(parent)
        , m_tooltip(new EraToolTipWidget)
    {
    }

    ~EraToolTipFilter() override
    {
        delete m_tooltip;
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        const QEvent::Type type = event->type();
        if (type == QEvent::ToolTip) {
            auto* helpEvent = static_cast<QHelpEvent*>(event);
            auto* widget = qobject_cast<QWidget*>(watched);
            if (widget) {
                const QString tip = widget->toolTip();
                if (!tip.isEmpty()) {
                    m_tooltip->showTooltip(tip, helpEvent->globalPos());
                    return true;
                }
            }
            m_tooltip->hide();
            return true;
        }
        if (type == QEvent::Leave
            || type == QEvent::MouseButtonPress
            || type == QEvent::KeyPress
            || type == QEvent::Wheel)
        {
            if (qobject_cast<QWidget*>(watched))
                m_tooltip->hide();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    EraToolTipWidget* m_tooltip{nullptr};
};

class EraOverlayScrollBar final : public QScrollBar
{
public:
    explicit EraOverlayScrollBar(Qt::Orientation orientation, QWidget* parent = nullptr)
        : QScrollBar(orientation, parent)
    {
        setAttribute(Qt::WA_Hover, true);
        setMouseTracking(true);
        setContextMenuPolicy(Qt::NoContextMenu);

        const int barThickness = kScrollBarExtent;
        if (orientation == Qt::Vertical)
        {
            setMinimumWidth(barThickness);
            setMaximumWidth(barThickness);
        }
        else
        {
            setMinimumHeight(barThickness);
            setMaximumHeight(barThickness);
        }

        setAutoFillBackground(false);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    }

    void setOpacity(qreal opacity)
    {
        const qreal clamped = std::clamp(opacity, 0.0, 1.0);
        if (std::abs(m_opacity - clamped) <= 0.001)
            return;

        m_opacity = clamped;
        update();
    }

    qreal opacity() const
    {
        return m_opacity;
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        if (m_opacity <= 0.001 || maximum() <= minimum())
            return;

        QStyleOptionSlider option;
        initStyleOption(&option);
        QRect handleRect = style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, this);
        if (!handleRect.isValid())
            return;

        if (orientation() == Qt::Vertical)
            handleRect.adjust(0, 1, 0, -1);
        else
            handleRect.adjust(1, 0, -1, 0);

        if (handleRect.width() <= 1 || handleRect.height() <= 1)
            return;

        const EraStyleColor::ThemePalette& t = EraStyleColor::themePalette();
        qreal alpha = kHandleBaseAlpha;
        QColor fill = t.scrollbarHandle;
        if (isSliderDown())
        {
            alpha = kHandlePressedAlpha;
            fill = t.scrollbarHandlePressed;
        }
        else if ((option.state & QStyle::State_MouseOver) || underMouse())
        {
            alpha = kHandleHoverAlpha;
            fill = t.scrollbarHandleHover;
        }

        fill.setAlphaF(std::clamp(alpha * m_opacity, 0.0, 1.0));

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);

        const QRectF rectF(handleRect);
        const qreal radius = orientation() == Qt::Vertical ? rectF.width() / 2.0 : rectF.height() / 2.0;
        painter.drawRoundedRect(rectF, radius, radius);
    }

private:
    qreal m_opacity{0.0};
};

EraOverlayScrollBar* ensureOverlayScrollBar(QAbstractScrollArea* area, Qt::Orientation orientation)
{
    if (!area)
        return nullptr;

    QScrollBar* current = orientation == Qt::Vertical ? area->verticalScrollBar() : area->horizontalScrollBar();
    if (auto* overlay = dynamic_cast<EraOverlayScrollBar*>(current))
        return overlay;

    auto* overlay = new EraOverlayScrollBar(orientation, area);
    if (orientation == Qt::Vertical)
        area->setVerticalScrollBar(overlay);
    else
        area->setHorizontalScrollBar(overlay);

    return overlay;
}

bool isHoverEventType(QEvent::Type type)
{
    return type == QEvent::Enter
        || type == QEvent::HoverEnter
        || type == QEvent::HoverMove
        || type == QEvent::MouseMove
        || type == QEvent::Wheel;
}

bool isLeaveEventType(QEvent::Type type)
{
    return type == QEvent::Leave || type == QEvent::HoverLeave;
}

class HoverScrollBarController final : public QObject
{
public:
    struct BarState
    {
        QPointer<EraOverlayScrollBar> bar;
        QVariantAnimation* animation{nullptr};
        bool enabled{false};
        qreal opacity{0.0};
        qreal targetOpacity{0.0};
    };

    HoverScrollBarController(QAbstractScrollArea* area, bool enableVertical, bool enableHorizontal)
        : QObject(area)
        , m_area(area)
        , m_enableVertical(enableVertical)
        , m_enableHorizontal(enableHorizontal)
    {
        m_hideTimer.setSingleShot(true);
        m_hideTimer.setInterval(kScrollBarHideDelayMs);
        connect(&m_hideTimer, &QTimer::timeout, area, [this] { syncVisibility(); });

        if (m_area)
        {
            m_area->setMouseTracking(true);
            m_area->setAttribute(Qt::WA_Hover, true);
            m_area->installEventFilter(this);
        }

        if (QWidget* viewport = m_area ? m_area->viewport() : nullptr)
        {
            viewport->setMouseTracking(true);
            viewport->setAttribute(Qt::WA_Hover, true);
            viewport->installEventFilter(this);
        }

        if (m_enableVertical && m_area)
            m_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        else if (m_area)
            m_area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        if (m_enableHorizontal && m_area)
            m_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        else if (m_area)
            m_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        setupBar(&m_verticalBar, m_enableVertical ? ensureOverlayScrollBar(m_area, Qt::Vertical) : nullptr, m_enableVertical);
        setupBar(&m_horizontalBar, m_enableHorizontal ? ensureOverlayScrollBar(m_area, Qt::Horizontal) : nullptr, m_enableHorizontal);
        syncVisibility();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        Q_UNUSED(watched);
        if (!m_area || !event)
            return QObject::eventFilter(watched, event);

        const QEvent::Type type = event->type();
        if (isHoverEventType(type))
        {
            m_hideTimer.stop();
            syncVisibility();
        }
        else if (isLeaveEventType(type))
        {
            scheduleVisibilitySync();
        }
        else if (type == QEvent::Show
                 || type == QEvent::Resize
                 || type == QEvent::Polish
                 || type == QEvent::PolishRequest
                 || type == QEvent::LayoutRequest
                 || type == QEvent::EnabledChange)
        {
            scheduleVisibilitySync();
        }
        else if (type == QEvent::Hide)
        {
            animateBarTo(&m_verticalBar, 0.0);
            animateBarTo(&m_horizontalBar, 0.0);
        }

        return QObject::eventFilter(watched, event);
    }

private:
    void setupBar(BarState* state, EraOverlayScrollBar* bar, bool enabled)
    {
        if (!state)
            return;

        state->bar = bar;
        state->enabled = enabled;

        if (!state->bar)
            return;

        ensureAnimation(state);

        state->bar->setVisible(false);
        applyBarStyle(state);

        state->bar->setMouseTracking(true);
        state->bar->setAttribute(Qt::WA_Hover, true);
        state->bar->installEventFilter(this);
        if (!enabled)
            state->bar->hide();

        connect(state->bar, &QScrollBar::rangeChanged, this, [this](int, int) {
            scheduleVisibilitySync();
        });
    }

    void ensureAnimation(BarState* state)
    {
        if (!state || state->animation)
            return;

        state->animation = new QVariantAnimation(this);
        state->animation->setDuration(kScrollBarFadeMs);
        state->animation->setEasingCurve(QEasingCurve::InOutCubic);

        connect(state->animation, &QVariantAnimation::valueChanged, this, [state](const QVariant& value) {
            if (!state)
                return;

            state->opacity = value.toReal();
            applyBarStyle(state);
        });

        connect(state->animation, &QVariantAnimation::finished, this, [state] {
            if (!state || !state->bar)
                return;

            state->opacity = state->targetOpacity;
            applyBarStyle(state);
            if (state->opacity <= 0.001)
                state->bar->setVisible(false);
        });
    }

    void scheduleVisibilitySync()
    {
        if (!m_hideTimer.isActive())
            m_hideTimer.start();
    }

    bool shouldShowBar(const BarState* state) const
    {
        return state
            && state->enabled
            && state->bar
            && m_area
            && m_area->isEnabled()
            && isPointerInsideScrollArea()
            && state->bar->maximum() > state->bar->minimum();
    }

    bool isPointerInsideScrollArea() const
    {
        if (!m_area)
            return false;

        return isWidgetHovered(m_area)
            || isWidgetHovered(m_area->viewport())
            || isWidgetHovered(m_area->verticalScrollBar())
            || isWidgetHovered(m_area->horizontalScrollBar());
    }

    static bool isWidgetHovered(const QWidget* widget)
    {
        return widget && widget->isVisible() && widget->underMouse();
    }

    static void applyBarStyle(const BarState* state)
    {
        if (!state || !state->bar)
            return;

        state->bar->setOpacity(state->opacity);
    }

    static void animateBarTo(BarState* state, qreal targetOpacity)
    {
        if (!state || !state->bar)
            return;

        state->targetOpacity = std::clamp(targetOpacity, 0.0, 1.0);
        if (std::abs(state->opacity - state->targetOpacity) <= 0.001)
        {
            if (state->targetOpacity <= 0.001)
                state->bar->setVisible(false);
            else
            {
                state->bar->setVisible(true);
                state->bar->raise();
            }
            return;
        }

        if (state->targetOpacity > 0.001)
        {
            state->bar->setVisible(true);
            state->bar->raise();
        }

        if (state->animation->state() == QAbstractAnimation::Running)
            state->animation->stop();

        state->animation->setStartValue(state->opacity);
        state->animation->setEndValue(state->targetOpacity);
        state->animation->start();
    }

    void syncVisibility()
    {
        if (!m_area)
            return;

        animateBarTo(&m_verticalBar, shouldShowBar(&m_verticalBar) ? 1.0 : 0.0);
        animateBarTo(&m_horizontalBar, shouldShowBar(&m_horizontalBar) ? 1.0 : 0.0);
    }

    QPointer<QAbstractScrollArea> m_area;
    QTimer m_hideTimer;
    bool m_enableVertical{true};
    bool m_enableHorizontal{true};
    BarState m_verticalBar;
    BarState m_horizontalBar;
};

}  // namespace

namespace EraStyle
{
QString normalizeThemeId(const QString& themeId)
{
    return normalizeThemeIdImpl(themeId);
}

QStringList availableThemeIds()
{
    return { QStringLiteral("era") };
}

void applyTheme(QApplication& app, const QString& themeId)
{
    const QString normalizedTheme = normalizeThemeIdImpl(themeId);
    app.setProperty(kCurrentThemeIdProperty, normalizedTheme);

    // Theme dispatcher hook.
    // Currently only the built-in era style exists; future styles can branch here.
    applyApplicationTheme(app);
}

void installApplicationStyle(QApplication& app, const QString& themeId)
{
    if (!app.property(kAppStyleInstalledProperty).toBool())
    {
        app.installEventFilter(new EraToolTipFilter(&app));
        if (!app.property(kThemeSyncInstalledProperty).toBool())
        {
            app.installEventFilter(new EraThemeSyncFilter(&app));
            app.setProperty(kThemeSyncInstalledProperty, true);
        }
        app.setProperty(kAppStyleInstalledProperty, true);
    }

    applyTheme(app, themeId);
}

void installHoverScrollBars(QAbstractScrollArea* area, bool enableVertical, bool enableHorizontal)
{
    if (!area || area->property(kScrollBarHelperInstalledProperty).toBool())
        return;

    area->setProperty(kScrollBarHelperInstalledProperty, true);
    new HoverScrollBarController(area, enableVertical, enableHorizontal);
}
}  // namespace EraStyle
