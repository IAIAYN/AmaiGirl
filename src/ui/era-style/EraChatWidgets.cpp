#include "ui/era-style/EraChatWidgets.hpp"

#include "ui/era-style/EraStyleColor.hpp"
#include "ui/era-style/EraStyleHelper.hpp"

#include <QAbstractTextDocumentLayout>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QStyleHints>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>

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
    menu->setAttribute(Qt::WA_TranslucentBackground);
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

EraChatBubbleTextView::EraChatBubbleTextView(bool isUserMessage, QWidget* parent)
    : QTextBrowser(parent)
    , m_isUserMessage(isUserMessage)
{
    init();
}

void EraChatBubbleTextView::setUserMessage(bool isUserMessage)
{
    if (m_isUserMessage == isUserMessage)
        return;

    m_isUserMessage = isUserMessage;
    refreshAppearance();
}

void EraChatBubbleTextView::changeEvent(QEvent* event)
{
    QTextBrowser::changeEvent(event);
    if (!event)
        return;

    if (isThemeEvent(event->type()) || event->type() == QEvent::EnabledChange)
    {
        QTimer::singleShot(0, this, [this] { refreshAppearance(); });
    }
}

void EraChatBubbleTextView::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu* menu = createStandardContextMenu();
    if (!menu)
        return;

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
    menu->setAttribute(Qt::WA_TranslucentBackground);
    for (QAction* a : menu->actions())
        a->setIcon(QIcon());
    menu->exec(event->globalPos());
    delete menu;
}

void EraChatBubbleTextView::init()
{
    setOpenExternalLinks(true);
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    document()->setDocumentMargin(0.0);
    setContentsMargins(0, 0, 0, 0);
    setWordWrapMode(QTextOption::WordWrap);
    setLineWrapMode(QTextEdit::WidgetWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

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
    const QColor textColor = m_isUserMessage ? pal.onAccentText : pal.textPrimary;
    const QColor disabledColor = pal.textDisabled;

    QPalette widgetPalette = palette();
    bool paletteChanged = false;
    const QColor resolvedTextColor = isEnabled() ? textColor : disabledColor;

    if (widgetPalette.color(QPalette::Text) != resolvedTextColor)
    {
        widgetPalette.setColor(QPalette::Text, resolvedTextColor);
        paletteChanged = true;
    }
    if (widgetPalette.color(QPalette::Base) != Qt::transparent)
    {
        widgetPalette.setColor(QPalette::Base, Qt::transparent);
        paletteChanged = true;
    }
    if (widgetPalette.color(QPalette::PlaceholderText) != disabledColor)
    {
        widgetPalette.setColor(QPalette::PlaceholderText, disabledColor);
        paletteChanged = true;
    }
    if (paletteChanged)
        setPalette(widgetPalette);

    viewport()->setAutoFillBackground(false);
    viewport()->update();
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

void EraChatListWidget::init()
{
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setFrameShape(QFrame::NoFrame);
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
