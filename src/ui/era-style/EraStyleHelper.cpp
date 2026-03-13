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
#include <QPen>
#include <QScreen>

namespace {
constexpr auto kAppStyleInstalledProperty = "_amaigirl_era_app_style_installed";
constexpr auto kScrollBarHelperInstalledProperty = "_amaigirl_era_scrollbar_helper_installed";
constexpr int kScrollBarHideDelayMs = 80;
constexpr int kScrollBarFadeMs = 140;
constexpr int kScrollBarExtent = 8;
constexpr int kScrollBarMargin = 2;
constexpr int kScrollBarMinHandle = 28;
constexpr qreal kHandleBaseAlpha = 0.360;
constexpr qreal kHandleHoverAlpha = 0.520;
constexpr qreal kHandlePressedAlpha = 0.640;
constexpr int kToolTipRadius = 8;

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
        const bool dark = EraStyleColor::isDark();
        painter.setBrush(dark ? EraStyleColor::DarkTooltipBackground : EraStyleColor::BasicWhite);
        painter.setPen(QPen(dark ? EraStyleColor::DarkPrimaryBorder : EraStyleColor::PrimaryBorder, 1.0));
        painter.drawRoundedRect(bgRect, kToolTipRadius, kToolTipRadius);

        painter.setPen(dark ? EraStyleColor::DarkMainText : EraStyleColor::MainText);
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

        qreal alpha = kHandleBaseAlpha;
        QColor fill = EraStyleColor::AuxiliaryText;
        if (isSliderDown())
        {
            alpha = kHandlePressedAlpha;
            fill = EraStyleColor::LinkClick;
        }
        else if ((option.state & QStyle::State_MouseOver) || underMouse())
        {
            alpha = kHandleHoverAlpha;
            fill = EraStyleColor::LinkHover;
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
void installApplicationStyle(QApplication& app)
{
    if (app.property(kAppStyleInstalledProperty).toBool())
        return;

    // Suppress native tooltip — EraToolTipFilter provides the custom rounded one.
    QString styleSheet = app.styleSheet();
    if (!styleSheet.isEmpty() && !styleSheet.endsWith(QLatin1Char('\n')))
        styleSheet += QLatin1Char('\n');
        styleSheet += QStringLiteral(
            "QToolTip { background: transparent; border: none; color: transparent; }"
        "QScrollBar { background: transparent; border: none; }"
        "QScrollBar:vertical { width: 8px; margin: 0px; }"
        "QScrollBar:horizontal { height: 8px; margin: 0px; }"
            "QScrollBar::groove:vertical { background: transparent; width: 0px; }"
            "QScrollBar::groove:horizontal { background: transparent; height: 0px; }"
            "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }"
            "QScrollBar::add-line { height: 0px; width: 0px; border: none; }"
            "QScrollBar::sub-line { height: 0px; width: 0px; border: none; }"
            "QScrollBar::handle { background: transparent; }"
        );
    app.setStyleSheet(styleSheet);

    app.installEventFilter(new EraToolTipFilter(&app));
    app.setProperty(kAppStyleInstalledProperty, true);
}

void installHoverScrollBars(QAbstractScrollArea* area, bool enableVertical, bool enableHorizontal)
{
    if (!area || area->property(kScrollBarHelperInstalledProperty).toBool())
        return;

    area->setProperty(kScrollBarHelperInstalledProperty, true);
    new HoverScrollBarController(area, enableVertical, enableHorizontal);
}
}  // namespace EraStyle