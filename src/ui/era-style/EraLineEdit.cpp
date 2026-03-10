#include "ui/era-style/EraLineEdit.hpp"
#include "ui/era-style/EraStyleColor.hpp"

#include <QEnterEvent>
#include <QFocusEvent>

namespace {
constexpr int kRadius = 4;
constexpr qreal kBorderWidth = 1.2;
constexpr int kPaddingH = 7;
constexpr int kPaddingV = 3;

QString toRgba(const QColor& color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(QString::number(color.alphaF(), 'f', 3));
}
}  // namespace

EraLineEdit::EraLineEdit(QWidget* parent)
    : QLineEdit(parent)
{
    init();
}

EraLineEdit::EraLineEdit(const QString& text, QWidget* parent)
    : QLineEdit(text, parent)
{
    init();
}

void EraLineEdit::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    updateColors();
    QLineEdit::enterEvent(event);
}

void EraLineEdit::leaveEvent(QEvent* event)
{
    m_hovered = false;
    updateColors();
    QLineEdit::leaveEvent(event);
}

void EraLineEdit::focusInEvent(QFocusEvent* event)
{
    QLineEdit::focusInEvent(event);
    updateColors();
}

void EraLineEdit::focusOutEvent(QFocusEvent* event)
{
    QLineEdit::focusOutEvent(event);
    updateColors();
}

void EraLineEdit::init()
{
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setMinimumHeight(32);
    updateColors();
}

void EraLineEdit::updateColors()
{
    if (!isEnabled())
    {
        m_borderColor = EraStyleColor::PrimaryBorder;
        m_textColor = EraStyleColor::DisabledText;
        m_placeholderColor = EraStyleColor::DisabledText;
    }
    else if (hasFocus())
    {
        m_borderColor = EraStyleColor::LinkClick;
        m_textColor = EraStyleColor::MainText;
        m_placeholderColor = EraStyleColor::AuxiliaryText;
    }
    else if (m_hovered)
    {
        m_borderColor = EraStyleColor::LinkHover;
        m_textColor = EraStyleColor::MainText;
        m_placeholderColor = EraStyleColor::AuxiliaryText;
    }
    else
    {
        m_borderColor = EraStyleColor::PrimaryBorder;
        m_textColor = EraStyleColor::MainText;
        m_placeholderColor = EraStyleColor::AuxiliaryText;
    }

    QPalette palette = this->palette();
    palette.setColor(QPalette::Text, m_textColor);
    palette.setColor(QPalette::PlaceholderText, m_placeholderColor);
    setPalette(palette);

    setStyleSheet(QStringLiteral(
        "QLineEdit {"
        " background: %1;"
        " color: %2;"
        " border: %3px solid %4;"
        " border-radius: %5px;"
        " padding: %6px %7px;"
        " }"
    )
        .arg(toRgba(isEnabled() ? EraStyleColor::BasicWhite : EraStyleColor::BasicGray))
        .arg(toRgba(m_textColor))
        .arg(QString::number(kBorderWidth, 'f', 1))
        .arg(toRgba(m_borderColor))
        .arg(kRadius)
        .arg(kPaddingV)
        .arg(kPaddingH));
}
