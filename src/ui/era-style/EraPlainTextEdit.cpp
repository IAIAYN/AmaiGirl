#include "ui/era-style/EraPlainTextEdit.hpp"
#include "ui/era-style/EraStyleColor.hpp"

#include <QEnterEvent>
#include <QFocusEvent>
#include <QTextDocument>

namespace {
constexpr int kRadius = 4;
constexpr qreal kBorderWidth = 1.2;
constexpr int kPaddingH = 7;
constexpr int kPaddingV = 3;
constexpr int kDocMargin = 1;

QString toRgba(const QColor& color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(QString::number(color.alphaF(), 'f', 3));
}
}  // namespace

EraPlainTextEdit::EraPlainTextEdit(QWidget* parent)
    : QPlainTextEdit(parent)
{
    init();
}

EraPlainTextEdit::EraPlainTextEdit(const QString& text, QWidget* parent)
    : QPlainTextEdit(parent)
{
    setPlainText(text);
    init();
}

void EraPlainTextEdit::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    updateColors();
    QPlainTextEdit::enterEvent(event);
}

void EraPlainTextEdit::leaveEvent(QEvent* event)
{
    m_hovered = false;
    updateColors();
    QPlainTextEdit::leaveEvent(event);
}

void EraPlainTextEdit::focusInEvent(QFocusEvent* event)
{
    QPlainTextEdit::focusInEvent(event);
    updateColors();
}

void EraPlainTextEdit::focusOutEvent(QFocusEvent* event)
{
    QPlainTextEdit::focusOutEvent(event);
    updateColors();
}

void EraPlainTextEdit::init()
{
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setFrameStyle(QFrame::NoFrame);
    document()->setDocumentMargin(kDocMargin);
    setMinimumHeight(96);
    updateColors();
}

void EraPlainTextEdit::updateColors()
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
        "QPlainTextEdit {"
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
