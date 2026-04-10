#include "ui/era-style/EraChatWidgets.hpp"

#include "ui/era-style/EraStyleColor.hpp"
#include "ui/era-style/EraStyleHelper.hpp"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcessEnvironment>
#include <QDebug>
#include <QResizeEvent>
#include <QStyleHints>
#include <QStyleOptionButton>
#include <QTextBlock>
#include <QTextBoundaryFinder>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextOption>
#include <QTimer>
#include <QWheelEvent>

#include <cmath>

namespace {
QString toRgba(const QColor& color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(QString::number(color.alphaF(), 'f', 3));
}

bool isThemeEvent(QEvent::Type type)
{
    return type == QEvent::ApplicationPaletteChange
        || type == QEvent::PaletteChange
        || type == QEvent::ThemeChange
        || type == QEvent::StyleChange;
}

bool chatLayoutDebugEnabled()
{
    static const bool enabled =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("AMAI_CHAT_LAYOUT_DEBUG")) == QStringLiteral("1");
    return enabled;
}

QString normalizeBubbleText(QString text)
{
    while (!text.isEmpty())
    {
        const QChar last = text.back();
        if (last != QLatin1Char('\n') && last != QLatin1Char('\r') && last != QChar::ParagraphSeparator)
            break;
        text.chop(1);
    }
    return text;
}

QPixmap tintedIconPixmap(const QIcon& icon, const QSize& logicalSize, const QColor& tint, qreal devicePixelRatio)
{
    if (icon.isNull())
        return {};

    const QSize deviceSize(
        qMax(1, qRound(logicalSize.width() * devicePixelRatio)),
        qMax(1, qRound(logicalSize.height() * devicePixelRatio))
    );

    QPixmap base = icon.pixmap(deviceSize);

    if (base.isNull())
        return {};

    base.setDevicePixelRatio(devicePixelRatio);
    QImage image = base.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(image.rect(), tint);

    return QPixmap::fromImage(image);
}

struct TextLayoutMetrics
{
    qreal maxLineWidth{0.0};
    qreal height{0.0};
    int lineCount{0};
};

struct TextLayoutParagraph
{
    int start{0};
    int length{0};
    int separatorLength{0};
    QString text;
};

bool isBubbleLineBreak(QChar ch)
{
    return ch == QLatin1Char('\n')
        || ch == QLatin1Char('\r')
        || ch == QChar::ParagraphSeparator
        || ch == QChar::LineSeparator;
}

QList<TextLayoutParagraph> splitTextLayoutParagraphs(const QString& text)
{
    QList<TextLayoutParagraph> paragraphs;
    if (text.isEmpty())
    {
        paragraphs.push_back({ 0, 0, 0, QString() });
        return paragraphs;
    }

    int start = 0;
    int index = 0;
    while (index < text.size())
    {
        if (!isBubbleLineBreak(text.at(index)))
        {
            ++index;
            continue;
        }

        int separatorLength = 1;
        if (text.at(index) == QLatin1Char('\r')
            && index + 1 < text.size()
            && text.at(index + 1) == QLatin1Char('\n'))
        {
            separatorLength = 2;
        }

        paragraphs.push_back({ start, index - start, separatorLength, text.mid(start, index - start) });
        index += separatorLength;
        start = index;
    }

    paragraphs.push_back({ start, int(text.size()) - start, 0, text.mid(start) });
    return paragraphs;
}

TextLayoutMetrics measureSingleTextLayout(const QString& text, const QFont& font, qreal width, QTextOption::WrapMode wrapMode)
{
    const QString content = text.isEmpty() ? QStringLiteral(" ") : text;

    QTextLayout layout(content, font);
    QTextOption option;
    option.setWrapMode(wrapMode);
    layout.setTextOption(option);

    const qreal lineWidth = wrapMode == QTextOption::NoWrap ? 1000000.0 : qMax(1.0, width);

    qreal y = 0.0;
    qreal maxLineWidth = 0.0;
    int lineCount = 0;

    layout.beginLayout();
    while (true)
    {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;

        line.setLineWidth(lineWidth);
        line.setPosition(QPointF(0.0, y));
        y += line.height();
        maxLineWidth = qMax(maxLineWidth, line.naturalTextWidth());
        ++lineCount;
    }
    layout.endLayout();

    const QFontMetricsF fm(font);
    if (lineCount == 0)
    {
        lineCount = 1;
        maxLineWidth = qMax(maxLineWidth, fm.horizontalAdvance(content));
        y = qMax(y, fm.height());
    }

    return { maxLineWidth, y, lineCount };
}

int layoutTextHeight(const TextLayoutMetrics& metrics, const QFont& font)
{
    const QFontMetrics fm(font);
    int textHeight = qMax(fm.height(), int(std::ceil(metrics.height)));
    const int leading = fm.leading();
    if (leading > 0 && textHeight > fm.height())
        textHeight -= leading;
    return textHeight;
}

TextLayoutMetrics measureTextLayout(const QString& text, const QFont& font, qreal width, QTextOption::WrapMode wrapMode)
{
    const QString content = text.isEmpty() ? QStringLiteral(" ") : text;
    const QList<TextLayoutParagraph> paragraphs = splitTextLayoutParagraphs(content);

    TextLayoutMetrics metrics;
    for (const TextLayoutParagraph& paragraph : paragraphs)
    {
        const TextLayoutMetrics paragraphMetrics = measureSingleTextLayout(paragraph.text, font, width, wrapMode);
        metrics.maxLineWidth = qMax(metrics.maxLineWidth, paragraphMetrics.maxLineWidth);
        metrics.height += paragraphMetrics.height;
        metrics.lineCount += paragraphMetrics.lineCount;
    }

    return metrics;
}

void drawTextLayout(QPainter* painter,
                    const QRectF& rect,
                    const QString& text,
                    const QFont& font,
                    const QColor& color,
                    QTextOption::WrapMode wrapMode,
                    int selectionStart = -1,
                    int selectionEnd = -1,
                    const QColor& selectionBackground = QColor(),
                    const QColor& selectionForeground = QColor())
{
    if (!painter)
        return;

    const QString content = text.isEmpty() ? QStringLiteral(" ") : text;
    painter->setPen(color);

    const qreal lineWidth = wrapMode == QTextOption::NoWrap ? 1000000.0 : qMax(1.0, rect.width());
    const QList<TextLayoutParagraph> paragraphs = splitTextLayoutParagraphs(content);
    qreal y = rect.y();

    for (const TextLayoutParagraph& paragraph : paragraphs)
    {
        const QString paragraphText = paragraph.text.isEmpty() ? QStringLiteral(" ") : paragraph.text;

        QTextLayout layout(paragraphText, font);
        QTextOption option;
        option.setWrapMode(wrapMode);
        layout.setTextOption(option);

        qreal paragraphHeight = 0.0;
        layout.beginLayout();
        while (true)
        {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;

            line.setLineWidth(lineWidth);
            line.setPosition(QPointF(rect.x(), y + paragraphHeight));
            paragraphHeight += line.height();
        }
        layout.endLayout();

        QList<QTextLayout::FormatRange> selections;
        if (selectionStart >= 0 && selectionEnd > selectionStart)
        {
            const int paragraphSelectionStart = qMax(selectionStart, paragraph.start);
            const int paragraphSelectionEnd = qMin(selectionEnd, paragraph.start + paragraph.length);
            if (paragraphSelectionEnd > paragraphSelectionStart)
            {
                QTextLayout::FormatRange range;
                range.start = paragraphSelectionStart - paragraph.start;
                range.length = paragraphSelectionEnd - paragraphSelectionStart;
                range.format.setBackground(selectionBackground);
                range.format.setForeground(selectionForeground);
                selections.push_back(range);
            }
        }

        layout.draw(painter, QPointF(0.0, 0.0), selections, rect);
        y += paragraphHeight;
    }
}

int hitTestTextLayout(const QString& text,
                      const QFont& font,
                      qreal width,
                      QTextOption::WrapMode wrapMode,
                      const QPointF& pos)
{
    const QString content = text.isEmpty() ? QStringLiteral(" ") : text;

    const qreal lineWidth = wrapMode == QTextOption::NoWrap ? 1000000.0 : qMax(1.0, width);
    const QList<TextLayoutParagraph> paragraphs = splitTextLayoutParagraphs(content);
    int lastCursorPosition = 0;
    qreal y = 0.0;

    for (const TextLayoutParagraph& paragraph : paragraphs)
    {
        const QString paragraphText = paragraph.text.isEmpty() ? QStringLiteral(" ") : paragraph.text;

        QTextLayout layout(paragraphText, font);
        QTextOption option;
        option.setWrapMode(wrapMode);
        layout.setTextOption(option);

        QTextLine lastLine;
        layout.beginLayout();
        while (true)
        {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;

            line.setLineWidth(lineWidth);
            line.setPosition(QPointF(0.0, y));
            y += line.height();
            lastLine = line;

            const QRectF lineRect(line.position(), QSizeF(lineWidth, line.height()));
            if (pos.y() >= lineRect.top() && pos.y() < lineRect.bottom())
            {
                layout.endLayout();
                const int localCursor = qBound(0,
                                               line.xToCursor(pos.x(), QTextLine::CursorBetweenCharacters),
                                               paragraph.length);
                return qMax(0, paragraph.start + localCursor);
            }
        }
        layout.endLayout();

        lastCursorPosition = paragraph.start + paragraph.length;
    }

    return qMax(0, lastCursorPosition);
}

QSize measureBubbleTextSize(const QString& text, const QFont& font, int maxWidth)
{
    const int boundedMaxWidth = qMax(1, maxWidth);
    const QString content = text.isEmpty() ? QStringLiteral(" ") : text;
    const TextLayoutMetrics noWrapMetrics = measureTextLayout(content, font, 0.0, QTextOption::NoWrap);

    const int tightWidth = qBound(1, int(std::ceil(noWrapMetrics.maxLineWidth)) + 8, boundedMaxWidth);
    const TextLayoutMetrics wrappedMetrics = measureTextLayout(content, font, tightWidth, QTextOption::WrapAtWordBoundaryOrAnywhere);

    return QSize(tightWidth, layoutTextHeight(wrappedMetrics, font));
}

qreal measureNaturalTextWidth(QTextDocument* doc, const QString& text, const QFont& font)
{
    Q_UNUSED(doc);
    return measureTextLayout(text, font, 0.0, QTextOption::NoWrap).maxLineWidth;
}

}  // namespace

EraChatComposerEdit::EraChatComposerEdit(QWidget* parent)
    : QTextEdit(parent)
{
    init();
}

int EraChatComposerEdit::preferredHeight(int minLines, int maxLines) const
{
    const QFontMetrics fm(font());
    const int lineHeight = fm.lineSpacing();
    const int minHeight = lineHeight * minLines;
    const int maxHeight = lineHeight * maxLines;
    const int docHeight = qMax(lineHeight, documentHeight());
    return qBound(minHeight, docHeight, maxHeight);
}

int EraChatComposerEdit::documentHeight() const
{
    const QFontMetrics fm(font());
    return qMax(
        fm.lineSpacing(),
        int(std::ceil(document()->documentLayout()->documentSize().height()))
    );
}

void EraChatComposerEdit::keyPressEvent(QKeyEvent* event)
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

void EraChatComposerEdit::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu* menu = createStandardContextMenu();
    if (!menu)
        return;
    for (QAction* a : menu->actions())
        a->setIcon(QIcon());
    menu->exec(event->globalPos());
    delete menu;
}

void EraChatComposerEdit::resizeEvent(QResizeEvent* event)
{
    QTextEdit::resizeEvent(event);
    emit metricsChanged();
}

void EraChatComposerEdit::changeEvent(QEvent* event)
{
    QTextEdit::changeEvent(event);
    if (!event)
        return;

    if (event->type() == QEvent::EnabledChange || isThemeEvent(event->type()))
    {
        QTimer::singleShot(0, this, [this] {
            refreshAppearance();
            emit metricsChanged();
        });
    }
}

void EraChatComposerEdit::init()
{
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setFrameStyle(QFrame::NoFrame);
    setAcceptRichText(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setContentsMargins(0, 0, 0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    document()->setDocumentMargin(0.0);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setLineWrapMode(QTextEdit::WidgetWidth);
    setStyleSheet(QStringLiteral(
        "QTextEdit{background:transparent; border:none; padding:0px; margin:0px;}"
    ));
    EraStyle::installHoverScrollBars(this, true, false);

    if (auto* layout = document()->documentLayout())
    {
        connect(layout, &QAbstractTextDocumentLayout::documentSizeChanged, this, [this](const QSizeF&) {
            emit metricsChanged();
        });
    }

    if (auto* hints = QGuiApplication::styleHints())
    {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
            QTimer::singleShot(0, this, [this] {
                refreshAppearance();
                emit metricsChanged();
            });
        });
    }

    refreshAppearance();
}

void EraChatComposerEdit::refreshAppearance()
{
    if (QCoreApplication::closingDown() || m_updatingColors)
        return;

    m_updatingColors = true;
    const EraStyleColor::ThemePalette& pal = EraStyleColor::themePalette();

    QPalette editPalette = palette();
    editPalette.setColor(QPalette::Text, isEnabled() ? pal.textPrimary : pal.textDisabled);
    editPalette.setColor(QPalette::PlaceholderText, isEnabled() ? pal.textMuted : pal.textDisabled);
    editPalette.setColor(QPalette::Highlight, pal.selectionBackground);
    editPalette.setColor(QPalette::HighlightedText, pal.selectionText);
    setPalette(editPalette);
    viewport()->setAutoFillBackground(false);
    viewport()->update();
    m_updatingColors = false;
}

EraIconToolButton::EraIconToolButton(QWidget* parent)
    : QToolButton(parent)
{
    init();
}

void EraIconToolButton::setTone(Tone tone)
{
    if (m_tone == tone)
        return;

    m_tone = tone;
    update();
}

void EraIconToolButton::setIconLogicalSize(int size)
{
    const int clamped = qMax(8, size);
    if (m_iconLogicalSize == clamped)
        return;

    m_iconLogicalSize = clamped;
    updateGeometry();
    update();
}

QSize EraIconToolButton::sizeHint() const
{
    const int side = qMax(m_iconLogicalSize + 12, 28);
    return QSize(side, side);
}

QSize EraIconToolButton::minimumSizeHint() const
{
    return sizeHint();
}

void EraIconToolButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const EraStyleColor::ThemePalette& pal = EraStyleColor::themePalette();
    QColor background = Qt::transparent;
    QColor iconColor = pal.textMuted;

    if (m_tone == Tone::Accent)
    {
        if (!isEnabled())
        {
            background = pal.inputBackgroundDisabled;
            iconColor = pal.textDisabled;
        }
        else if (m_pressed)
        {
            background = pal.accentPressed;
            iconColor = pal.onAccentText;
        }
        else if (m_hovered)
        {
            background = pal.accentHover;
            iconColor = pal.onAccentText;
        }
        else
        {
            background = pal.accent;
            iconColor = pal.onAccentText;
        }
    }
    else if (m_tone == Tone::Danger)
    {
        if (!isEnabled())
        {
            background = pal.inputBackgroundDisabled;
            iconColor = pal.textDisabled;
        }
        else if (m_pressed)
        {
            background = pal.dangerPressed;
            iconColor = pal.onAccentText;
        }
        else if (m_hovered)
        {
            background = pal.dangerHover;
            iconColor = pal.onAccentText;
        }
        else
        {
            background = pal.danger;
            iconColor = pal.onAccentText;
        }
    }
    else
    {
        if (!isEnabled())
        {
            background = Qt::transparent;
            iconColor = pal.textDisabled;
        }
        else if (m_pressed)
        {
            background = pal.tabActiveBackground;
            iconColor = pal.textPrimary;
        }
        else if (m_hovered)
        {
            background = pal.hoverBackground;
            iconColor = pal.textPrimary;
        }
        else
        {
            background = Qt::transparent;
            iconColor = pal.textMuted;
        }
    }

    const QRectF buttonRect = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(buttonRect, buttonRect.height() / 2.0, buttonRect.height() / 2.0);

    const int logicalIconSize = qMin(m_iconLogicalSize, qMax(8, qMin(width(), height()) - 6));
    if (!icon().isNull())
    {
        const QRect iconRect(
            (width() - logicalIconSize) / 2,
            (height() - logicalIconSize) / 2,
            logicalIconSize,
            logicalIconSize
        );

        const QPixmap pix = tintedIconPixmap(icon(), QSize(logicalIconSize, logicalIconSize), iconColor, devicePixelRatioF());
        if (!pix.isNull())
            painter.drawPixmap(iconRect, pix);
    }
    else if (!text().isEmpty())
    {
        painter.setPen(iconColor);
        painter.drawText(rect(), Qt::AlignCenter, text());
    }
}

void EraIconToolButton::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QToolButton::enterEvent(event);
}

void EraIconToolButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    m_pressed = false;
    update();
    QToolButton::leaveEvent(event);
}

void EraIconToolButton::mousePressEvent(QMouseEvent* event)
{
    QToolButton::mousePressEvent(event);
    m_pressed = isDown();
    update();
}

void EraIconToolButton::mouseReleaseEvent(QMouseEvent* event)
{
    QToolButton::mouseReleaseEvent(event);
    m_pressed = isDown();
    update();
}

void EraIconToolButton::changeEvent(QEvent* event)
{
    QToolButton::changeEvent(event);
    if (event && (event->type() == QEvent::EnabledChange || isThemeEvent(event->type())))
        update();
}

void EraIconToolButton::init()
{
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setAutoRaise(true);
    setMouseTracking(true);
}

EraChatBubbleBox::EraChatBubbleBox(bool isUserBubble, QWidget* parent)
    : QWidget(parent)
    , m_isUserBubble(isUserBubble)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
}

void EraChatBubbleBox::setUserBubble(bool isUserBubble)
{
    if (m_isUserBubble == isUserBubble)
        return;

    m_isUserBubble = isUserBubble;
    update();
}

void EraChatBubbleBox::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const EraStyleColor::ThemePalette& pal = EraStyleColor::themePalette();
    QColor background;
    QColor border;
    if (m_isUserBubble)
    {
        background = pal.accent;
        border = pal.accentPressed;
    }
    else
    {
        background = EraStyleColor::isDark() ? pal.panelRaised : pal.panelBackground;
        border = EraStyleColor::isDark() ? pal.borderPrimary : pal.borderSecondary;
    }

    painter.setPen(QPen(border, 1.0));
    painter.setBrush(background);
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 14, 14);
}

void EraChatBubbleBox::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event && isThemeEvent(event->type()))
        update();
}

EraChatSelectionCheckBox::EraChatSelectionCheckBox(QWidget* parent)
    : QCheckBox(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setText(QString());
    setTristate(false);
}

void EraChatSelectionCheckBox::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const EraStyleColor::ThemePalette& pal = EraStyleColor::themePalette();
    const bool checked = isChecked();
    const bool hovered = underMouse();

    QColor fill = Qt::transparent;
    QColor border = pal.borderPrimary;
    if (!isEnabled())
    {
        border = pal.textDisabled;
        if (checked)
            fill = pal.textDisabled;
    }
    else if (checked)
    {
        fill = hovered ? pal.accentHover : pal.accent;
        border = fill;
    }
    else if (hovered)
    {
        fill = pal.hoverBackground;
        border = pal.accent;
    }

    const int side = qMax(10, qMin(width(), height()) - 2);
    const QRectF r((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    p.setPen(QPen(border, 1.4));
    p.setBrush(fill);
    p.drawEllipse(r);

    if (checked)
    {
        QPainterPath tick;
        const qreal x = r.left();
        const qreal y = r.top();
        const qreal w = r.width();
        const qreal h = r.height();
        tick.moveTo(x + w * 0.25, y + h * 0.55);
        tick.lineTo(x + w * 0.45, y + h * 0.74);
        tick.lineTo(x + w * 0.76, y + h * 0.32);
        p.setPen(QPen(EraStyleColor::themePalette().onAccentText, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(tick);
    }
}

void EraChatSelectionCheckBox::changeEvent(QEvent* event)
{
    QCheckBox::changeEvent(event);
    if (event && (event->type() == QEvent::EnabledChange || isThemeEvent(event->type())))
        update();
}

EraChatBubbleTextView::EraChatBubbleTextView(bool isUserMessage, QWidget* parent)
    : QWidget(parent)
    , m_doc(std::make_unique<QTextDocument>(this))
    , m_isUserMessage(isUserMessage)
{
    init();
}

void EraChatBubbleTextView::setPlainText(const QString& text)
{
    if (!m_doc)
        return;

    const QString normalized = normalizeBubbleText(text);
    if (normalizeBubbleText(m_doc->toPlainText()) == normalized)
        return;

    m_doc->setPlainText(normalized);
    clearSelection();
    refreshLayoutWidth();
    updateGeometry();
    update();
}

void EraChatBubbleTextView::appendPlainText(const QString& text)
{
    if (!m_doc || text.isEmpty())
        return;

    const QString updated = normalizeBubbleText(m_doc->toPlainText() + text);
    m_doc->setPlainText(updated);
    m_anchorPosition = m_cursorPosition = m_doc->characterCount() - 1;
    refreshLayoutWidth();
    updateGeometry();
    update();
}

QString EraChatBubbleTextView::toPlainText() const
{
    return m_doc ? m_doc->toPlainText() : QString();
}

void EraChatBubbleTextView::selectAll()
{
    if (!m_doc)
        return;

    const int endPos = qMax(0, m_doc->characterCount() - 1);
    if (endPos <= 0)
    {
        clearSelection();
        return;
    }

    setSelectionRange(0, endPos);
}

void EraChatBubbleTextView::copy() const
{
    if (!m_doc || !hasSelection())
        return;

    QTextCursor cursor(m_doc.get());
    const int start = qMin(m_anchorPosition, m_cursorPosition);
    const int end = qMax(m_anchorPosition, m_cursorPosition);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);

    QString selected = cursor.selectedText();
    selected.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    if (auto* clipboard = QApplication::clipboard())
        clipboard->setText(selected);
}

bool EraChatBubbleTextView::hasSelection() const
{
    return m_anchorPosition >= 0
        && m_cursorPosition >= 0
        && m_anchorPosition != m_cursorPosition;
}

void EraChatBubbleTextView::setUserMessage(bool isUserMessage)
{
    if (m_isUserMessage == isUserMessage)
        return;

    m_isUserMessage = isUserMessage;
    refreshAppearance();
}

void EraChatBubbleTextView::setMessageSelectionState(int sourceMessageIndex, bool checked)
{
    m_sourceMessageIndex = sourceMessageIndex;
    m_messageChecked = checked;
}

QSize EraChatBubbleTextView::measureForMaxWidth(int maxWidth) const
{
    return measureBubbleTextSize(toPlainText(), font(), maxWidth);
}

QSize EraChatBubbleTextView::layoutForMaxWidth(int maxWidth)
{
    if (!m_doc)
        return {};

    const int boundedMaxWidth = qMax(1, maxWidth);
    const QString content = toPlainText().isEmpty() ? QStringLiteral(" ") : toPlainText();
    const bool hasHardBreak = content.contains(QLatin1Char('\n'))
        || content.contains(QLatin1Char('\r'))
        || content.contains(QChar::ParagraphSeparator)
        || content.contains(QChar::LineSeparator);

    QTextOption opt = m_doc->defaultTextOption();
    opt.setWrapMode(QTextOption::NoWrap);
    m_doc->setDefaultTextOption(opt);
    m_doc->setTextWidth(-1.0);
    m_doc->adjustSize();

    const qreal naturalWidth = measureNaturalTextWidth(m_doc.get(), content, font());
    const qreal noWrapDocWidth = m_doc->documentLayout()
        ? m_doc->documentLayout()->documentSize().width()
        : naturalWidth;
    const int contentWidthHint = int(std::ceil(qMax(naturalWidth, noWrapDocWidth)));
    const int estimatedWidth = qBound(1, contentWidthHint + 12, boundedMaxWidth);
    const bool shouldKeepSingleLine = !hasHardBreak && (contentWidthHint + 12 <= boundedMaxWidth);

    int finalWidth = estimatedWidth;
    int lineCount = 1;

    if (shouldKeepSingleLine)
    {
        m_layoutWrapMode = QTextOption::NoWrap;
        m_layoutTextWidth = finalWidth;
        opt.setWrapMode(QTextOption::NoWrap);
        m_doc->setDefaultTextOption(opt);
        m_doc->setTextWidth(-1.0);
        m_doc->adjustSize();
        m_layoutSize = QSize(finalWidth, QFontMetrics(font()).height());
    }
    else
    {
        finalWidth = estimatedWidth;
        m_layoutWrapMode = QTextOption::WrapAtWordBoundaryOrAnywhere;
        m_layoutTextWidth = finalWidth;
        opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        m_doc->setDefaultTextOption(opt);
        m_doc->setTextWidth(finalWidth);
        m_doc->adjustSize();
        const TextLayoutMetrics wrappedMetrics = measureTextLayout(content, font(), finalWidth, QTextOption::WrapAtWordBoundaryOrAnywhere);
        lineCount = wrappedMetrics.lineCount;
        m_layoutSize = QSize(finalWidth, layoutTextHeight(wrappedMetrics, font()));
    }

    if (size() != m_layoutSize)
        setFixedSize(m_layoutSize);

    if (chatLayoutDebugEnabled())
    {
        qInfo().noquote()
            << "[chat-layout][text]"
            << "text=" << content.left(80).replace(QLatin1Char('\n'), QLatin1Char(' '))
            << "maxW=" << boundedMaxWidth
            << "naturalW=" << int(std::ceil(naturalWidth))
            << "noWrapDocW=" << int(std::ceil(noWrapDocWidth))
            << "estimatedW=" << estimatedWidth
            << "finalW=" << finalWidth
            << "finalH=" << m_layoutSize.height()
            << "lines=" << lineCount;
    }

    updateGeometry();
    update();
    return m_layoutSize;
}

QSizeF EraChatBubbleTextView::textSizeForWidth(int width)
{
    if (!m_doc)
        return {};

    const int layoutWidth = qMax(1, width);
    QTextOption opt = m_doc->defaultTextOption();
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_doc->setDefaultTextOption(opt);
    m_doc->setTextWidth(layoutWidth);
    m_doc->adjustSize();

    const TextLayoutMetrics wrappedMetrics = measureTextLayout(toPlainText(), font(), layoutWidth, QTextOption::WrapAtWordBoundaryOrAnywhere);
    const int textHeight = layoutTextHeight(wrappedMetrics, font());

    m_layoutSize = QSize(layoutWidth, textHeight);
    if (size() != m_layoutSize)
        setFixedSize(m_layoutSize);

    updateGeometry();
    update();
    return QSizeF(layoutWidth, textHeight);
}

QSize EraChatBubbleTextView::sizeHint() const
{
    return m_layoutSize;
}

QSize EraChatBubbleTextView::minimumSizeHint() const
{
    return m_layoutSize;
}

void EraChatBubbleTextView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (!m_doc)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setClipRect(rect());

    const QColor drawColor = isEnabled() ? m_textColor : m_disabledTextColor;
    const EraStyleColor::ThemePalette& pal = EraStyleColor::themePalette();
    const int selectionStart = hasSelection() ? qMin(m_anchorPosition, m_cursorPosition) : -1;
    const int selectionEnd = hasSelection() ? qMax(m_anchorPosition, m_cursorPosition) : -1;

    drawTextLayout(&painter,
                   QRectF(rect()),
                   toPlainText(),
                   font(),
                   drawColor,
                   m_layoutWrapMode,
                   selectionStart,
                   selectionEnd,
                   pal.selectionBackground,
                   pal.selectionText);
}

void EraChatBubbleTextView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!event)
        return;

    if (event->size().width() != event->oldSize().width())
        refreshLayoutWidth();
}

void EraChatBubbleTextView::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (!event)
        return;

    if (event->type() == QEvent::FontChange && m_doc)
    {
        m_doc->setDefaultFont(font());
        refreshLayoutWidth();
        updateGeometry();
        update();
    }

    if (isThemeEvent(event->type()) || event->type() == QEvent::EnabledChange || event->type() == QEvent::FontChange)
    {
        QTimer::singleShot(0, this, [this] { refreshAppearance(); });
    }
}

void EraChatBubbleTextView::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu* menu = new QMenu(this);
    if (!menu)
        return;

    QAction* copyAction = menu->addAction(tr("复制"));
    QAction* selectAllAction = menu->addAction(tr("全选"));
    copyAction->setEnabled(hasSelection());
    selectAllAction->setEnabled(!toPlainText().isEmpty());

    QAction* selectMessageAction = nullptr;
    if (m_sourceMessageIndex >= 0)
    {
        menu->addSeparator();
        selectMessageAction = menu->addAction(m_messageChecked ? tr("取消勾选") : tr("选择消息"));
    }

    const EraStyleColor::ThemePalette& pal = EraStyleColor::themePalette();
    QPalette menuPalette = menu->palette();
    menuPalette.setColor(QPalette::Window, pal.popupBackground);
    menuPalette.setColor(QPalette::Base, pal.popupBackground);
    menuPalette.setColor(QPalette::Button, pal.popupBackground);
    menuPalette.setColor(QPalette::Text, pal.textPrimary);
    menuPalette.setColor(QPalette::WindowText, pal.textPrimary);
    menuPalette.setColor(QPalette::Highlight, pal.hoverBackground);
    menuPalette.setColor(QPalette::HighlightedText, pal.textPrimary);
    menu->setPalette(menuPalette);
    menu->setAutoFillBackground(true);
    menu->setAttribute(Qt::WA_StyledBackground, true);
    menu->setStyleSheet(QStringLiteral(
        "QMenu{background:%1; color:%2; border:1px solid %3; border-radius:8px; padding:4px;}"
        "QMenu::item{background:%1; color:%2; padding:6px 18px 6px 12px; border-radius:4px;}"
        "QMenu::item:selected{background:%4; color:%2;}"
        "QMenu::item:disabled{background:%1; color:%5;}"
        "QMenu::indicator{width:0px; height:0px;}"
    )
                            .arg(toRgba(pal.popupBackground))
                            .arg(toRgba(pal.textPrimary))
                            .arg(toRgba(pal.divider))
                            .arg(toRgba(pal.hoverBackground))
                            .arg(toRgba(pal.textDisabled)));
    for (QAction* a : menu->actions())
        a->setIcon(QIcon());

    QAction* chosen = menu->exec(event->globalPos());
    if (chosen == copyAction)
        copy();
    else if (chosen == selectAllAction)
        selectAll();
    else if (chosen == selectMessageAction && m_sourceMessageIndex >= 0)
        emit requestToggleMessageSelection(m_sourceMessageIndex, !m_messageChecked);

    delete menu;
}

void EraChatBubbleTextView::mousePressEvent(QMouseEvent* event)
{
    if (!event)
        return;

    if (event->button() == Qt::LeftButton)
    {
        trackLeftClick(event->position().toPoint(), event->timestamp(), false);
        setFocus(Qt::MouseFocusReason);
        const int pos = hitTestPosition(event->pos());

        if (m_leftClickStreak >= 3)
        {
            m_selectionGranularity = SelectionGranularity::Paragraph;
            m_selectionAnchorBounds = paragraphBoundsAtPosition(pos);
            selectParagraphAtPosition(pos);
            m_dragSelecting = true;
            event->accept();
            return;
        }

        m_selectionGranularity = SelectionGranularity::Character;
        m_selectionAnchorBounds = {};
        m_dragSelecting = true;
        setSelectionRange(pos, pos);
        event->accept();
        return;
    }

    m_leftClickStreak = 0;
    m_selectionGranularity = SelectionGranularity::Character;
    m_selectionAnchorBounds = {};

    QWidget::mousePressEvent(event);
}

void EraChatBubbleTextView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!event)
        return;

    if (event->button() == Qt::LeftButton)
    {
        trackLeftClick(event->position().toPoint(), event->timestamp(), true);
        setFocus(Qt::MouseFocusReason);
        const int pos = hitTestPosition(event->pos());
        m_selectionGranularity = SelectionGranularity::Word;
        m_selectionAnchorBounds = wordBoundsAtPosition(pos);
        selectWordAtPosition(pos);
        m_dragSelecting = true;
        event->accept();
        return;
    }

    QWidget::mouseDoubleClickEvent(event);
}

void EraChatBubbleTextView::mouseMoveEvent(QMouseEvent* event)
{
    if (!event)
        return;

    if (m_dragSelecting && (event->buttons() & Qt::LeftButton))
    {
        const int pos = hitTestPosition(event->pos());
        if (m_selectionGranularity == SelectionGranularity::Word && m_selectionAnchorBounds.isValid())
        {
            const SelectionBounds currentBounds = wordBoundsAtPosition(pos);
            if (currentBounds.isValid())
            {
                if (currentBounds.start < m_selectionAnchorBounds.start)
                    setSelectionRange(currentBounds.start, m_selectionAnchorBounds.end);
                else
                    setSelectionRange(m_selectionAnchorBounds.start, currentBounds.end);
            }
        }
        else if (m_selectionGranularity == SelectionGranularity::Paragraph && m_selectionAnchorBounds.isValid())
        {
            const SelectionBounds currentBounds = paragraphBoundsAtPosition(pos);
            if (currentBounds.isValid())
            {
                if (currentBounds.start < m_selectionAnchorBounds.start)
                    setSelectionRange(currentBounds.start, m_selectionAnchorBounds.end);
                else
                    setSelectionRange(m_selectionAnchorBounds.start, currentBounds.end);
            }
        }
        else
        {
            setSelectionRange(m_anchorPosition, pos);
        }
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void EraChatBubbleTextView::mouseReleaseEvent(QMouseEvent* event)
{
    if (!event)
        return;

    if (event->button() == Qt::LeftButton)
        m_dragSelecting = false;

    QWidget::mouseReleaseEvent(event);
}

void EraChatBubbleTextView::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    if (event && event->reason() == Qt::PopupFocusReason)
        return;

    if (hasSelection())
        clearSelection();
}

void EraChatBubbleTextView::keyPressEvent(QKeyEvent* event)
{
    if (!event)
        return;

    if (event->matches(QKeySequence::Copy))
    {
        copy();
        event->accept();
        return;
    }

    if (event->matches(QKeySequence::SelectAll))
    {
        selectAll();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void EraChatBubbleTextView::wheelEvent(QWheelEvent* event)
{
    // Bubble content must never behave as an internal scroll area.
    event->ignore();
}

int EraChatBubbleTextView::hitTestPosition(const QPoint& pos) const
{
    if (!m_doc)
        return 0;

    const int layoutWidth = m_layoutWrapMode == QTextOption::NoWrap ? width() : m_layoutTextWidth;
    int hit = hitTestTextLayout(toPlainText(),
                                font(),
                                qMax(1, layoutWidth),
                                m_layoutWrapMode,
                                QPointF(pos));
    const int maxPos = qMax(0, m_doc->characterCount() - 1);
    if (hit < 0)
        hit = maxPos;
    return qBound(0, hit, maxPos);
}

EraChatBubbleTextView::SelectionBounds EraChatBubbleTextView::wordBoundsAtPosition(int position) const
{
    if (!m_doc)
        return {};

    const QString content = toPlainText();
    if (content.isEmpty())
        return {};

    int lookupPos = qBound(0, position, content.size());
    if (lookupPos == content.size() && lookupPos > 0)
        --lookupPos;

    QTextBoundaryFinder finder(QTextBoundaryFinder::Word, content);
    finder.setPosition(lookupPos);
    int start = finder.toPreviousBoundary();
    finder.setPosition(lookupPos);
    int end = finder.toNextBoundary();

    if (start < 0)
        start = 0;
    if (end < 0)
        end = content.size();

    while (start < end && content.at(start).isSpace())
        ++start;
    while (end > start && content.at(end - 1).isSpace())
        --end;

    if (start == end)
    {
        start = qBound(0, lookupPos, content.size() - 1);
        end = qMin(content.size(), start + 1);
    }

    return { start, end };
}

EraChatBubbleTextView::SelectionBounds EraChatBubbleTextView::paragraphBoundsAtPosition(int position) const
{
    if (!m_doc)
        return {};

    const QString content = toPlainText();
    if (content.isEmpty())
        return {};

    const int lookupPos = qBound(0, position, content.size());
    int start = lookupPos;
    int end = lookupPos;

    while (start > 0)
    {
        const QChar ch = content.at(start - 1);
        if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r') || ch == QChar::ParagraphSeparator)
            break;
        --start;
    }

    while (end < content.size())
    {
        const QChar ch = content.at(end);
        if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r') || ch == QChar::ParagraphSeparator)
            break;
        ++end;
    }

    return { start, end };
}

void EraChatBubbleTextView::setSelectionRange(int anchor, int position)
{
    if (!m_doc)
        return;

    const int maxPos = qMax(0, m_doc->characterCount() - 1);
    m_anchorPosition = qBound(0, anchor, maxPos);
    m_cursorPosition = qBound(0, position, maxPos);
    update();
}

void EraChatBubbleTextView::clearSelection()
{
    m_anchorPosition = -1;
    m_cursorPosition = -1;
    m_selectionAnchorBounds = {};
    m_selectionGranularity = SelectionGranularity::Character;
    update();
}

void EraChatBubbleTextView::selectWordAtPosition(int position)
{
    const SelectionBounds bounds = wordBoundsAtPosition(position);
    if (!bounds.isValid())
    {
        clearSelection();
        return;
    }

    setSelectionRange(bounds.start, bounds.end);
}

void EraChatBubbleTextView::selectParagraphAtPosition(int position)
{
    const SelectionBounds bounds = paragraphBoundsAtPosition(position);
    if (!bounds.isValid())
    {
        clearSelection();
        return;
    }

    setSelectionRange(bounds.start, bounds.end);
}

void EraChatBubbleTextView::trackLeftClick(const QPoint& pos, ulong timestamp, bool isDoubleClick)
{
    const int interval = QGuiApplication::styleHints()
        ? QGuiApplication::styleHints()->mouseDoubleClickInterval()
        : 400;
    const int distance = QApplication::startDragDistance();

    const bool repeatedClick = m_hasLastClick
        && timestamp >= m_lastClickTimestamp
        && (timestamp - m_lastClickTimestamp) <= ulong(interval)
        && (m_lastClickPos - pos).manhattanLength() <= distance;

    if (isDoubleClick)
        m_leftClickStreak = repeatedClick ? qMax(2, m_leftClickStreak + 1) : 2;
    else
        m_leftClickStreak = repeatedClick ? (m_leftClickStreak + 1) : 1;

    m_lastClickPos = pos;
    m_lastClickTimestamp = timestamp;
    m_hasLastClick = true;
}

void EraChatBubbleTextView::refreshLayoutWidth()
{
    if (!m_doc)
        return;

    const int layoutWidth = qMax(1, width());
    QTextOption opt = m_doc->defaultTextOption();
    opt.setWrapMode(m_layoutWrapMode);
    m_doc->setDefaultTextOption(opt);
    if (m_layoutWrapMode == QTextOption::NoWrap)
    {
        m_doc->setTextWidth(-1.0);
        m_layoutSize = QSize(layoutWidth, QFontMetrics(font()).height());
    }
    else
    {
        if (!qFuzzyCompare(m_doc->textWidth(), qreal(layoutWidth)))
            m_doc->setTextWidth(layoutWidth);
        const TextLayoutMetrics wrappedMetrics = measureTextLayout(toPlainText(), font(), layoutWidth, QTextOption::WrapAtWordBoundaryOrAnywhere);
        m_layoutSize = QSize(layoutWidth, layoutTextHeight(wrappedMetrics, font()));
    }
    m_doc->adjustSize();
    m_layoutTextWidth = layoutWidth;

    updateGeometry();
    update();
}

void EraChatBubbleTextView::init()
{
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setFocusPolicy(Qt::ClickFocus);
    setCursor(Qt::IBeamCursor);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setContentsMargins(0, 0, 0, 0);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    if (m_doc)
    {
        m_doc->setDocumentMargin(0.0);
        m_doc->setDefaultFont(font());
        QTextOption opt;
        opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        m_doc->setDefaultTextOption(opt);
        m_doc->setTextWidth(1.0);
    }
    m_layoutSize = QSize(1, QFontMetrics(font()).height());

    if (auto* hints = QGuiApplication::styleHints())
    {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
            QTimer::singleShot(0, this, [this] { refreshAppearance(); });
        });
    }

    refreshAppearance();
}

void EraChatBubbleTextView::refreshAppearance()
{
    if (QCoreApplication::closingDown() || m_updatingColors)
        return;

    m_updatingColors = true;
    const EraStyleColor::ThemePalette& pal = EraStyleColor::themePalette();
    m_textColor = m_isUserMessage ? pal.onAccentText : pal.textPrimary;
    m_disabledTextColor = pal.textDisabled;

    QPalette widgetPalette = palette();
    bool paletteChanged = false;
    const QColor resolvedTextColor = isEnabled() ? m_textColor : m_disabledTextColor;

    if (widgetPalette.color(QPalette::Text) != resolvedTextColor)
    {
        widgetPalette.setColor(QPalette::Text, resolvedTextColor);
        paletteChanged = true;
    }
    if (widgetPalette.color(QPalette::PlaceholderText) != m_disabledTextColor)
    {
        widgetPalette.setColor(QPalette::PlaceholderText, m_disabledTextColor);
        paletteChanged = true;
    }
    if (widgetPalette.color(QPalette::Highlight) != pal.hoverBackground)
    {
        widgetPalette.setColor(QPalette::Highlight, pal.hoverBackground);
        paletteChanged = true;
    }
    if (widgetPalette.color(QPalette::HighlightedText) != resolvedTextColor)
    {
        widgetPalette.setColor(QPalette::HighlightedText, resolvedTextColor);
        paletteChanged = true;
    }
    if (paletteChanged)
        setPalette(widgetPalette);

    update();
    m_updatingColors = false;
}

EraChatListWidget::EraChatListWidget(QWidget* parent)
    : QListWidget(parent)
{
    init();
}

void EraChatListWidget::changeEvent(QEvent* event)
{
    QListWidget::changeEvent(event);
    if (!event)
        return;

    if (isThemeEvent(event->type()) || event->type() == QEvent::EnabledChange)
    {
        QTimer::singleShot(0, this, [this] { refreshAppearance(); });
    }
}

void EraChatListWidget::mousePressEvent(QMouseEvent* event)
{
    if (event && !itemAt(event->position().toPoint()))
    {
        if (QWidget* focused = QApplication::focusWidget())
        {
            if (focused != this && (isAncestorOf(focused) || (viewport() && viewport()->isAncestorOf(focused))))
                focused->clearFocus();
        }
    }

    QListWidget::mousePressEvent(event);
}

void EraChatListWidget::init()
{
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setFrameShape(QFrame::NoFrame);
    setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    setStyleSheet(QStringLiteral(
        "QListWidget{background:transparent; border:none; outline:none; padding:0px;}"
        "QListWidget::item{background:transparent; border:none; margin:0px; padding:0px;}"
    ));

    if (viewport())
    {
        viewport()->setAutoFillBackground(false);
        viewport()->setStyleSheet(QStringLiteral("background:transparent; border:none;"));
    }

    EraStyle::installHoverScrollBars(this, true, false);

    if (auto* hints = QGuiApplication::styleHints())
    {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
            QTimer::singleShot(0, this, [this] { refreshAppearance(); });
        });
    }

    refreshAppearance();
}

void EraChatListWidget::refreshAppearance()
{
    if (viewport())
    {
        viewport()->setAutoFillBackground(false);
        viewport()->update();
    }

    update();
}
