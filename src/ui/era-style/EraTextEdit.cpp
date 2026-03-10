#include "ui/era-style/EraTextEdit.hpp"
#include "ui/era-style/EraStyleColor.hpp"

#include <QEnterEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QTextDocument>

namespace {
constexpr int kRadius = 4;
constexpr qreal kBorderWidth = 1.2;
constexpr int kPaddingH = 8;
constexpr int kPaddingV = 6;
constexpr int kDocMargin = 1;

QString toRgba(const QColor& color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(QString::number(color.alphaF(), 'f', 3));
}
}

EraTextEdit::EraTextEdit(QWidget* parent)
    : QTextEdit(parent)
{
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setFrameStyle(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setContentsMargins(0, 0, 0, 0);
    document()->setDocumentMargin(kDocMargin);
    updateColors();
}


void EraTextEdit::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && !(event->modifiers() & Qt::ShiftModifier))
    {
        event->accept();
        emit sendRequested();
        return;
    }

    QTextEdit::keyPressEvent(event);
}

void EraTextEdit::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    updateColors();
    QTextEdit::enterEvent(event);
}

void EraTextEdit::leaveEvent(QEvent* event)
{
    m_hovered = false;
    updateColors();
    QTextEdit::leaveEvent(event);
}

void EraTextEdit::focusInEvent(QFocusEvent* event)
{
    QTextEdit::focusInEvent(event);
    updateColors();
}

void EraTextEdit::focusOutEvent(QFocusEvent* event)
{
    QTextEdit::focusOutEvent(event);
    updateColors();
}


void EraTextEdit::updateColors()
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
        "QTextEdit {"
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

    viewport()->update();
}