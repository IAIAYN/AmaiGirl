#include "ui/era-style/EraLineEdit.hpp"
#include "ui/era-style/EraStyleColor.hpp"

#include <QEnterEvent>
#include <QFocusEvent>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QStyleHints>
#include <QTimer>

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

void EraLineEdit::changeEvent(QEvent* event)
{
    QLineEdit::changeEvent(event);
    if (!event)
        return;

    const QEvent::Type type = event->type();
    if (type == QEvent::ApplicationPaletteChange
        || type == QEvent::PaletteChange
        || type == QEvent::ThemeChange
        || type == QEvent::StyleChange)
    {
        QTimer::singleShot(0, this, [this] { updateColors(); });
    }
}

void EraLineEdit::init()
{
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setMinimumHeight(32);
    if (auto* hints = QGuiApplication::styleHints())
    {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
            QTimer::singleShot(0, this, [this] { updateColors(); });
        });
    }
    updateColors();
}

void EraLineEdit::updateColors()
{
    if (QCoreApplication::closingDown() || m_updatingColors)
        return;
    m_updatingColors = true;

    const EraStyleColor::ThemePalette& t = EraStyleColor::themePalette();

    if (!isEnabled())
    {
        m_borderColor = t.borderSecondary;
        m_textColor = t.textDisabled;
        m_placeholderColor = t.textDisabled;
    }
    else if (hasFocus())
    {
        m_borderColor = t.accentPressed;
        m_textColor = t.textPrimary;
        m_placeholderColor = t.textMuted;
    }
    else if (m_hovered)
    {
        m_borderColor = t.accentHover;
        m_textColor = t.textPrimary;
        m_placeholderColor = t.textMuted;
    }
    else
    {
        m_borderColor = t.borderPrimary;
        m_textColor = t.textPrimary;
        m_placeholderColor = t.textMuted;
    }

    QPalette palette = this->palette();
    palette.setColor(QPalette::Text, m_textColor);
    palette.setColor(QPalette::PlaceholderText, m_placeholderColor);
    setPalette(palette);

    const QColor bgColor = !isEnabled() ? t.inputBackgroundDisabled : t.inputBackground;

    const QString styleSheet = QStringLiteral(
        "QLineEdit {"
        " background: %1;"
        " color: %2;"
        " border: %3px solid %4;"
        " border-radius: %5px;"
        " padding: %6px %7px;"
        " }"
    )
        .arg(toRgba(bgColor))
        .arg(toRgba(m_textColor))
        .arg(QString::number(kBorderWidth, 'f', 1))
        .arg(toRgba(m_borderColor))
        .arg(kRadius)
        .arg(kPaddingV)
        .arg(kPaddingH);

    if (m_lastStyleSheet != styleSheet)
    {
        m_lastStyleSheet = styleSheet;
        setStyleSheet(styleSheet);
    }

    update();
    m_updatingColors = false;
}
