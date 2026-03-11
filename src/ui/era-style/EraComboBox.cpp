#include "ui/era-style/EraComboBox.hpp"
#include "ui/era-style/EraStyleColor.hpp"

#include <QAbstractItemView>
#include <QFocusEvent>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QVariantAnimation>
#include <QEnterEvent>
#include <algorithm>

namespace {
constexpr int kRadius = 4;
constexpr qreal kBorderWidth = 1.5;
constexpr int kPaddingH = 10;
constexpr int kArrowAreaW = 26;
constexpr int kArrowSize = 5;
constexpr int kMinHeight = 26;
constexpr int kAnimMs = 120;
constexpr int kPopupItemHeight = 26;

class EraComboItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), kPopupItemHeight));
        return size;
    }
};
}  // namespace

EraComboBox::EraComboBox(QWidget* parent)
    : QComboBox(parent)
{
    init();
}

QSize EraComboBox::sizeHint() const
{
    int maxTextWidth = 0;
    for (int i = 0; i < count(); ++i)
        maxTextWidth = std::max(maxTextWidth, fontMetrics().horizontalAdvance(itemText(i)));

    if (maxTextWidth == 0)
        maxTextWidth = fontMetrics().horizontalAdvance(currentText().isEmpty() ? placeholderText() : currentText());

    const int w = std::max(kPaddingH * 2 + kArrowAreaW + maxTextWidth, 120);
    const int h = std::max(minimumHeight(), kMinHeight);
    return QSize(w, h);
}

QSize EraComboBox::minimumSizeHint() const
{
    const int w = std::max(kPaddingH * 2 + kArrowAreaW, 88);
    const int h = std::max(minimumHeight(), kMinHeight);
    return QSize(w, h);
}

void EraComboBox::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QRectF rectF = rect();
    rectF.adjust(0.75, 0.75, -0.75, -0.75);

    painter.setPen(Qt::NoPen);
    painter.setBrush(isEnabled() ? EraStyleColor::BasicWhite : EraStyleColor::BasicGray);
    painter.drawRoundedRect(rectF, kRadius, kRadius);

    painter.setPen(QPen(m_borderColor, kBorderWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rectF, kRadius, kRadius);

    const bool hasCurrent = currentIndex() >= 0 && !currentText().isEmpty();
    const QString text = hasCurrent ? currentText() : placeholderText();
    QColor textColor = !isEnabled() ? EraStyleColor::DisabledText : (hasCurrent ? EraStyleColor::MainText : EraStyleColor::DisabledText);
    painter.setPen(textColor);
    painter.setFont(font());

    const QRect textRect(kPaddingH, 0, width() - kPaddingH * 2 - kArrowAreaW, height());
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text);

    const int arrowCenterX = width() - kArrowAreaW / 2;
    const int arrowCenterY = height() / 2 + (view() && view()->isVisible() ? 1 : 0);
    QPainterPath arrowPath;
    if (view() && view()->isVisible())
    {
        arrowPath.moveTo(arrowCenterX - kArrowSize, arrowCenterY + 2);
        arrowPath.lineTo(arrowCenterX, arrowCenterY - kArrowSize / 2);
        arrowPath.lineTo(arrowCenterX + kArrowSize, arrowCenterY + 2);
    }
    else
    {
        arrowPath.moveTo(arrowCenterX - kArrowSize, arrowCenterY - 2);
        arrowPath.lineTo(arrowCenterX, arrowCenterY - 2 - kArrowSize / 2);
        arrowPath.lineTo(arrowCenterX + kArrowSize, arrowCenterY - 2);
    }

    painter.setPen(QPen(isEnabled() ? EraStyleColor::AuxiliaryText : EraStyleColor::DisabledText, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(arrowPath);
}

void EraComboBox::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    updateColors(true);
    QComboBox::enterEvent(event);
}

void EraComboBox::leaveEvent(QEvent* event)
{
    m_hovered = false;
    updateColors(true);
    QComboBox::leaveEvent(event);
}

void EraComboBox::focusInEvent(QFocusEvent* event)
{
    QComboBox::focusInEvent(event);
    updateColors(true);
}

void EraComboBox::focusOutEvent(QFocusEvent* event)
{
    QComboBox::focusOutEvent(event);
    updateColors(true);
}

void EraComboBox::showPopup()
{
    refreshPopupStyle();
    if (view())
    {
        const int contentWidth = view()->sizeHintForColumn(0) + 40;
        view()->setMinimumWidth(std::max(width(), contentWidth));
    }
    QComboBox::showPopup();
    updateColors(false);
    update();
}

void EraComboBox::init()
{
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(kMinHeight);
    setMinimumWidth(88);
    setSizeAdjustPolicy(QComboBox::AdjustToContentsOnFirstShow);
    setCursor(Qt::PointingHandCursor);
    setEditable(false);
    setInsertPolicy(QComboBox::NoInsert);
    setIconSize(QSize(0, 0));

    auto* listView = new QListView(this);
    listView->setItemDelegate(new EraComboItemDelegate(listView));
    listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    listView->setTextElideMode(Qt::ElideRight);
    setView(listView);

    setStyleSheet(QStringLiteral(
        "QComboBox { background: transparent; border: none; padding: 0; }"
        "QComboBox::drop-down { border: none; width: 0px; }"
        "QComboBox::down-arrow { image: none; }"
    ));

    view()->setFrameShape(QFrame::NoFrame);
    view()->viewport()->setAutoFillBackground(false);
    view()->setStyleSheet(QStringLiteral(
        "QListView {"
        " background: %1;"
        " color: %2;"
        " border: 1px solid %3;"
        " border-radius: %4px;"
        " outline: none;"
        " padding: 4px 0px;"
        " }"
        " QListView::item {"
        " border: none;"
        " padding: 6px 12px;"
        " min-height: %5px;"
        " background: transparent;"
        " }"
        " QListView::item:hover {"
        " background: %6;"
        " }"
        " QListView::item:selected {"
        " background: %6;"
        " color: %2;"
        " }"
        " QScrollBar:vertical { width: 8px; background: transparent; margin: 6px 4px 6px 0px; }"
        " QScrollBar::handle:vertical { background: %7; border-radius: 4px; min-height: 24px; }"
        " QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,"
        " QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { height: 0px; background: transparent; }"
    )
        .arg(QStringLiteral("rgba(%1, %2, %3, %4)").arg(EraStyleColor::BasicWhite.red()).arg(EraStyleColor::BasicWhite.green()).arg(EraStyleColor::BasicWhite.blue()).arg(QString::number(EraStyleColor::BasicWhite.alphaF(), 'f', 3)))
        .arg(QStringLiteral("rgba(%1, %2, %3, %4)").arg(EraStyleColor::MainText.red()).arg(EraStyleColor::MainText.green()).arg(EraStyleColor::MainText.blue()).arg(QString::number(EraStyleColor::MainText.alphaF(), 'f', 3)))
        .arg(QStringLiteral("rgba(%1, %2, %3, %4)").arg(EraStyleColor::SecondaryBorder.red()).arg(EraStyleColor::SecondaryBorder.green()).arg(EraStyleColor::SecondaryBorder.blue()).arg(QString::number(EraStyleColor::SecondaryBorder.alphaF(), 'f', 3)))
        .arg(kRadius)
        .arg(kPopupItemHeight - 10)
        .arg(QStringLiteral("rgba(%1, %2, %3, %4)").arg(EraStyleColor::BasicGray.red()).arg(EraStyleColor::BasicGray.green()).arg(EraStyleColor::BasicGray.blue()).arg(QString::number(EraStyleColor::BasicGray.alphaF(), 'f', 3)))
        .arg(QStringLiteral("rgba(%1, %2, %3, %4)").arg(EraStyleColor::PrimaryBorder.red()).arg(EraStyleColor::PrimaryBorder.green()).arg(EraStyleColor::PrimaryBorder.blue()).arg(QString::number(EraStyleColor::PrimaryBorder.alphaF(), 'f', 3))));

    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(kAnimMs);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        const qreal t = value.toReal();
        m_borderColor = blend(m_animStartBorder, m_targetBorderColor, t);
        update();
    });

    connect(this, &QComboBox::currentIndexChanged, this, [this](int) { update(); });
    updateColors(false);
}

void EraComboBox::updateColors(bool animated)
{
    QColor target = EraStyleColor::PrimaryBorder;
    if (!isEnabled())
        target = EraStyleColor::SecondaryBorder;
    else if (hasFocus() || (view() && view()->isVisible()))
        target = EraStyleColor::Link;
    else if (m_hovered)
        target = EraStyleColor::LinkHover;

    animateTo(target, animated);
}

void EraComboBox::animateTo(const QColor& border, bool animated)
{
    m_targetBorderColor = border;

    if (!animated)
    {
        if (m_anim->state() == QAbstractAnimation::Running)
            m_anim->stop();
        m_borderColor = border;
        update();
        return;
    }

    m_animStartBorder = m_borderColor.isValid() ? m_borderColor : border;
    if (m_anim->state() == QAbstractAnimation::Running)
        m_anim->stop();
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->start();
}

void EraComboBox::refreshPopupStyle()
{
    if (!view())
        return;
    view()->window()->setAttribute(Qt::WA_MacShowFocusRect, false);
    view()->update();
}

QColor EraComboBox::blend(const QColor& from, const QColor& to, qreal t)
{
    const qreal x = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(
        from.redF() + (to.redF() - from.redF()) * x,
        from.greenF() + (to.greenF() - from.greenF()) * x,
        from.blueF() + (to.blueF() - from.blueF()) * x,
        from.alphaF() + (to.alphaF() - from.alphaF()) * x
    );
}
