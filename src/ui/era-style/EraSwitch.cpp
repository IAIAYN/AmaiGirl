#include "ui/era-style/EraSwitch.hpp"
#include "ui/era-style/EraStyleColor.hpp"

#include <QVariantAnimation>
#include <QPainter>
#include <QPaintEvent>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QFontMetrics>
#include <algorithm>

namespace {
constexpr int kTrackHeight = 24;
constexpr int kTrackWidth = 48;
constexpr int kTrackInset = 2;
constexpr int kTextSpacing = 8;
constexpr int kAnimMs = 100;
}

EraSwitch::EraSwitch(const QString& text, QWidget* parent)
    : QCheckBox(text, parent)
{
    init();
}

EraSwitch::EraSwitch(QWidget* parent)
    : QCheckBox(parent)
{
    init();
}

QSize EraSwitch::sizeHint() const
{
    const QFontMetrics fm(font());
    const int textWidth = text().isEmpty() ? 0 : fm.horizontalAdvance(text());
    const int textHeight = fm.height();
    const int width = kTrackWidth + (text().isEmpty() ? 0 : (kTextSpacing + textWidth));
    const int height = std::max(kTrackHeight, textHeight);
    return {width, height};
}

QSize EraSwitch::minimumSizeHint() const
{
    return sizeHint();
}

void EraSwitch::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF fullRect = rect();
    const qreal trackY = std::round((fullRect.height() - kTrackHeight) * 0.5);
    QRectF trackRect(0.0, trackY, kTrackWidth, kTrackHeight);
    trackRect.adjust(0.5, 0.5, -0.5, -0.5);

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_trackColor);
    painter.drawRoundedRect(trackRect, trackRect.height() * 0.5, trackRect.height() * 0.5);

    const qreal knobDiameter = trackRect.height() - kTrackInset * 2.0;
    const qreal knobRadius = knobDiameter * 0.5;
    const qreal knobTravel = trackRect.width() - knobDiameter - kTrackInset * 2.0;
    const qreal knobX = trackRect.left() + kTrackInset + knobTravel * m_thumbT;
    const qreal knobY = trackRect.top() + kTrackInset;

    const EraStyleColor::ThemePalette& t = EraStyleColor::themePalette();
    painter.setBrush(isEnabled() ? t.panelRaised : t.inputBackgroundDisabled);
    painter.drawEllipse(QRectF(knobX, knobY, knobDiameter, knobDiameter));

    if (!text().isEmpty())
    {
        const auto group = isEnabled() ? QPalette::Active : QPalette::Disabled;
        painter.setPen(palette().color(group, QPalette::ButtonText));
        const QRect textRect(kTrackWidth + kTextSpacing, 0, width() - kTrackWidth - kTextSpacing, height());
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text());
    }
}

void EraSwitch::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    updateTargetState(true);
    QCheckBox::enterEvent(event);
}

void EraSwitch::leaveEvent(QEvent* event)
{
    m_hovered = false;
    m_pressed = false;
    updateTargetState(true);
    QCheckBox::leaveEvent(event);
}

void EraSwitch::mousePressEvent(QMouseEvent* event)
{
    QCheckBox::mousePressEvent(event);
    m_pressed = true;
    updateTargetState(true);
}

void EraSwitch::mouseReleaseEvent(QMouseEvent* event)
{
    QCheckBox::mouseReleaseEvent(event);
    m_pressed = false;
    updateTargetState(true);
}

void EraSwitch::changeEvent(QEvent* event)
{
    QCheckBox::changeEvent(event);
    updateTargetState(false);
}

bool EraSwitch::hitButton(const QPoint& pos) const
{
    return rect().contains(pos);
}

void EraSwitch::init()
{
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    setMinimumHeight(kTrackHeight);

    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(kAnimMs);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        const qreal t = value.toReal();
        m_thumbT = m_animStartThumbT + (m_targetThumbT - m_animStartThumbT) * t;
        m_trackColor = blend(m_animStartTrackColor, m_targetTrackColor, t);
        update();
    });

    connect(this, &QCheckBox::toggled, this, [this](bool) {
        updateTargetState(true);
    });

    updateTargetState(false);
}

void EraSwitch::updateTargetState(bool animated)
{
    const EraStyleColor::ThemePalette& t = EraStyleColor::themePalette();
    QColor targetTrackColor = t.textDisabled;
    qreal targetThumbT = isChecked() ? 1.0 : 0.0;

    if (!isEnabled())
    {
        targetTrackColor = isChecked() ? t.borderPrimary : t.borderSecondary;
    }
    else if (isChecked())
    {
        targetTrackColor = m_pressed ? t.accentPressed : (m_hovered ? t.accentHover : t.accent);
    }
    else
    {
        targetTrackColor = m_pressed ? t.textMuted : (m_hovered ? t.textSecondary : t.textDisabled);
    }

    animateTo(targetThumbT, targetTrackColor, animated);
}

void EraSwitch::animateTo(qreal thumbT, const QColor& trackColor, bool animated)
{
    m_targetThumbT = thumbT;
    m_targetTrackColor = trackColor;

    if (!animated)
    {
        if (m_anim->state() == QAbstractAnimation::Running)
            m_anim->stop();
        m_thumbT = thumbT;
        m_trackColor = trackColor;
        update();
        return;
    }

    m_animStartThumbT = m_thumbT;
    m_animStartTrackColor = m_trackColor.isValid() ? m_trackColor : trackColor;

    if (m_anim->state() == QAbstractAnimation::Running)
        m_anim->stop();

    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->start();
}

QColor EraSwitch::blend(const QColor& from, const QColor& to, qreal t)
{
    const qreal x = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(
        from.redF() + (to.redF() - from.redF()) * x,
        from.greenF() + (to.greenF() - from.greenF()) * x,
        from.blueF() + (to.blueF() - from.blueF()) * x,
        from.alphaF() + (to.alphaF() - from.alphaF()) * x
    );
}
