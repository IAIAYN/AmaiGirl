#include "ui/era-style/EraButton.hpp"
#include "ui/era-style/EraStyleColor.hpp"

#include <QVariantAnimation>
#include <QPainter>
#include <QPaintEvent>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QtGui/qcolor.h>
#include <algorithm>

namespace {
constexpr int kRadius = 4;
constexpr qreal kBorderWidth = 1.2;
constexpr int kAnimMs = 120;
}

EraButton::EraButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent)
{
    init();
}

EraButton::EraButton(QWidget* parent)
    : QPushButton(parent)
{
    init();
}

void EraButton::setTone(Tone tone)
{
    if (m_tone == tone)
        return;

    m_tone = tone;
    updateTargetState(false);
    update();
}

void EraButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QRectF rect(this->rect());
    rect.adjust(0.6, 0.6, -0.6, -0.6);
    const Palette pal = paletteForTone();

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_backgroundColor);
    painter.drawRoundedRect(rect, kRadius, kRadius);

    QPen pen(m_borderColor, kBorderWidth);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect, kRadius, kRadius);

    painter.setPen(m_textColor);
    painter.setFont(font());

    const QRect textRect = rect.toRect().adjusted(12, 0, -12, 0);
    painter.drawText(textRect, Qt::AlignCenter, text());
}

void EraButton::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    updateTargetState(true);
    QPushButton::enterEvent(event);
}

void EraButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    m_pressed = false;
    updateTargetState(true);
    QPushButton::leaveEvent(event);
}

void EraButton::mousePressEvent(QMouseEvent* event)
{
    QPushButton::mousePressEvent(event);
    m_pressed = isDown();
    updateTargetState(true);
}

void EraButton::mouseReleaseEvent(QMouseEvent* event)
{
    QPushButton::mouseReleaseEvent(event);
    m_pressed = isDown();
    updateTargetState(true);
}

QSize EraButton::sizeHint() const
{
    const int textWidth = fontMetrics().horizontalAdvance(text());
    const int w = textWidth + 24; // 左右各 12px padding
    const int h = qMax(minimumHeight(), 32);
    return QSize(w, h);
}

QSize EraButton::minimumSizeHint() const
{
    return sizeHint();
}

void EraButton::changeEvent(QEvent* event)
{
    QPushButton::changeEvent(event);
    updateTargetState(false);
}

void EraButton::init()
{
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setFlat(true);
    setMinimumHeight(32);

    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(kAnimMs);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        const qreal t = value.toReal();
        m_borderColor = blend(m_animStartBorder, m_targetBorderColor, t);
        m_textColor = blend(m_animStartText, m_targetTextColor, t);
        m_backgroundColor = blend(m_animStartBackground, m_targetBackgroundColor, t);
        update();
    });

    updateTargetState(false);
}

void EraButton::updateTargetState(bool animated)
{
    const Palette pal = paletteForTone();
    const EraStyleColor::ThemePalette& t = EraStyleColor::themePalette();

    QColor targetBorder;
    QColor targetText;
    QColor targetBackground;

    if (!isEnabled())
    {
        targetBorder = t.borderSecondary;
        targetText = t.textDisabled;
        targetBackground = t.inputBackgroundDisabled;
    }
    else if (m_pressed && m_hovered)
    {
        targetBorder = pal.borderPressed;
        targetText = pal.textPressed;
        targetBackground = pal.backgroundPressed;
    }
    else if (m_hovered)
    {
        targetBorder = pal.borderHover;
        targetText = pal.textHover;
        targetBackground = pal.backgroundHover;
    }
    else
    {
        targetBorder = pal.borderDefault;
        targetText = pal.textDefault;
        targetBackground = pal.backgroundDefault;
    }

    animateTo(targetBorder, targetText, targetBackground, animated);
}

void EraButton::animateTo(const QColor& border, const QColor& text, const QColor& background,  bool animated)
{
    m_targetBorderColor = border;
    m_targetTextColor = text;
    m_targetBackgroundColor = background;

    if (!animated)
    {
        if (m_anim->state() == QAbstractAnimation::Running)
            m_anim->stop();
        m_borderColor = border;
        m_textColor = text;
        m_backgroundColor = background;
        update();
        return;
    }

    m_animStartBorder = m_borderColor.isValid() ? m_borderColor : border;
    m_animStartText = m_textColor.isValid() ? m_textColor : text;
    m_animStartBackground = m_backgroundColor.isValid() ? m_backgroundColor : background;

    if (m_anim->state() == QAbstractAnimation::Running)
        m_anim->stop();

    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->start();
}

EraButton::Palette EraButton::paletteForTone() const
{
    const EraStyleColor::ThemePalette& t = EraStyleColor::themePalette();

    switch (m_tone)
    {
    case Tone::Link:
        return {t.accent, t.accentHover, t.accentPressed,
            t.onAccentText, t.onAccentText, t.onAccentText, t.textDisabled,
            t.accent, t.accentHover, t.accentPressed};
    case Tone::Success:
        return {t.success, t.successHover, t.successPressed,
            t.onAccentText, t.onAccentText, t.onAccentText, t.textDisabled,
            t.success, t.successHover, t.successPressed};
    case Tone::Warning:
        return {t.warning, t.warningHover, t.warningPressed,
            t.onAccentText, t.onAccentText, t.onAccentText, t.textDisabled,
            t.warning, t.warningHover, t.warningPressed};
    case Tone::Danger:
        return {t.danger, t.dangerHover, t.dangerPressed,
            t.onAccentText, t.onAccentText, t.onAccentText, t.textDisabled,
            t.danger, t.dangerHover, t.dangerPressed};
    case Tone::Info:
        return {t.info, t.infoHover, t.infoPressed,
            t.onAccentText, t.onAccentText, t.onAccentText, t.textDisabled,
            t.info, t.infoHover, t.infoPressed};
    case Tone::Neutral:
    default: {
        return {t.borderPrimary,
            t.accentHover, t.accentPressed,
            t.textSecondary,
            t.accentHover, t.accentPressed,
            t.textDisabled,
            t.inputBackground,
            t.hoverBackground,
            t.panelRaised};
    }
    }
}

QColor EraButton::blend(const QColor& from, const QColor& to, qreal t)
{
    const qreal x = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(
        from.redF() + (to.redF() - from.redF()) * x,
        from.greenF() + (to.greenF() - from.greenF()) * x,
        from.blueF() + (to.blueF() - from.blueF()) * x,
        from.alphaF() + (to.alphaF() - from.alphaF()) * x
    );
}
