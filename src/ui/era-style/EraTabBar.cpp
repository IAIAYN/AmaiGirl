#include "ui/era-style/EraTabBar.hpp"
#include "ui/era-style/EraStyleColor.hpp"

#include <QVariantAnimation>
#include <QPainter>
#include <QPaintEvent>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QEasingCurve>
#include <QPixmap>
#include <QImage>

namespace {
constexpr int kTabPaddingH = 14;
constexpr int kTabIconSize = 18;
constexpr int kTabIconGap = 8;
constexpr int kVerticalContentInsetL = 11;
constexpr int kVerticalContentInsetR = 20;
constexpr int kHorizontalBarHeight = 38;
constexpr int kVerticalTabMinHeight = 50;
constexpr int kVerticalTabExtraPadding = 20;
constexpr int kIndicatorH  = 2;
constexpr int kSepH        = 1;
constexpr int kVerticalDividerW = 1;
constexpr int kAnimMs      = 330;
constexpr int kVerticalBarMinWidth = 112;

QPixmap tintedIconPixmap(const QIcon& icon, const QSize& logicalSize, const QColor& tint, qreal dpr)
{
    if (icon.isNull())
        return {};

    const QSize deviceSize(
        qMax(1, qRound(logicalSize.width() * dpr)),
        qMax(1, qRound(logicalSize.height() * dpr))
    );

    QPixmap base = icon.pixmap(deviceSize);
    if (base.isNull())
        return {};

    base.setDevicePixelRatio(dpr);
    QImage image = base.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QPainter imagePainter(&image);
    imagePainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    imagePainter.fillRect(image.rect(), tint);

    return QPixmap::fromImage(image);
}
} // namespace

EraTabBar::EraTabBar(QWidget* parent)
    : QWidget(parent)
{
    init();
}

void EraTabBar::addTab(const QString& label)
{
    addTab(label, QIcon());
}

void EraTabBar::addTab(const QString& label, const QIcon& icon)
{
    m_labels.append(label);
    m_icons.append(icon);

    if (m_labels.size() == 1)
    {
        const TabGeom g = tabGeomAt(0);
        m_indicatorX = g.x;
        m_indicatorW = g.width;
        m_targetX    = g.x;
        m_targetW    = g.width;
    }

    if (m_orientation == Orientation::Vertical)
    {
        applyOrientationGeometry();
        updateGeometry();
    }

    update();
}

void EraTabBar::setTabText(int index, const QString& label)
{
    if (index < 0 || index >= m_labels.size())
        return;

    m_labels[index] = label;
    if (index == m_currentIndex)
    {
        const TabGeom g = tabGeomAt(m_currentIndex);
        m_indicatorX = g.x;
        m_indicatorW = g.width;
        m_targetX    = g.x;
        m_targetW    = g.width;
    }

    if (m_orientation == Orientation::Vertical)
    {
        applyOrientationGeometry();
        updateGeometry();
    }

    update();
}

void EraTabBar::setTabIcon(int index, const QIcon& icon)
{
    if (index < 0 || index >= m_labels.size())
        return;

    if (m_icons.size() < m_labels.size())
        m_icons.resize(m_labels.size());

    m_icons[index] = icon;

    if (m_orientation == Orientation::Vertical)
    {
        applyOrientationGeometry();
        updateGeometry();
    }

    update();
}

void EraTabBar::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_labels.size())
        return;
    if (index == m_currentIndex && m_anim->state() != QAbstractAnimation::Running)
        return;

    m_currentIndex = index;
    animateIndicatorTo(index);
    emit currentChanged(index);
}

void EraTabBar::setOrientation(Orientation orientation)
{
    if (m_orientation == orientation)
        return;

    m_orientation = orientation;
    applyOrientationGeometry();

    if (!m_labels.isEmpty())
    {
        const TabGeom g = tabGeomAt(m_currentIndex);
        m_indicatorX = g.x;
        m_indicatorW = g.width;
        m_targetX = g.x;
        m_targetW = g.width;
    }

    updateGeometry();
    update();
}

QSize EraTabBar::sizeHint() const
{
    if (m_orientation == Orientation::Vertical)
    {
        const QFontMetrics fm(font());
        const int tabH = verticalTabHeight();
        int maxTextW = 0;
        bool hasAnyIcon = false;
        for (int i = 0; i < m_labels.size(); ++i)
        {
            maxTextW = qMax(maxTextW, fm.horizontalAdvance(m_labels.at(i)));
            if (i < m_icons.size() && !m_icons.at(i).isNull())
                hasAnyIcon = true;
        }

        const int iconSlotW = hasAnyIcon ? (kTabIconSize + kTabIconGap) : 0;
        const int maxW = maxTextW + iconSlotW;

        const int width = qMax(
            maxW + kVerticalContentInsetL + kVerticalContentInsetR + kIndicatorH + kVerticalDividerW,
            kVerticalBarMinWidth
        );
        const int height = qMax(tabH, m_labels.size() * tabH);
        return QSize(width, height);
    }

    int totalW = 0;
    const QFontMetrics fm(font());
    for (int i = 0; i < m_labels.size(); ++i)
        totalW += tabContentWidthAt(i, fm) + kTabPaddingH * 2;
    return QSize(qMax(totalW, 80), kHorizontalBarHeight);
}

QSize EraTabBar::minimumSizeHint() const
{
    if (m_orientation == Orientation::Vertical)
        return QSize(sizeHint().width(), verticalTabHeight());

    return QSize(80, kHorizontalBarHeight);
}

void EraTabBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int W = width();
    const int H = height();
    const EraStyleColor::ThemePalette& pal = EraStyleColor::themePalette();

    painter.setPen(Qt::NoPen);
    if (m_orientation == Orientation::Vertical)
    {
        painter.fillRect(QRect(W - kVerticalDividerW, 0, kVerticalDividerW, H), pal.tabDivider);
    }
    else
    {
        painter.setBrush(pal.tabDivider);
        painter.drawRect(0, H - kSepH, W, kSepH);
    }

    const QFontMetrics fm(font());
    bool hasAnyIcon = false;
    for (int i = 0; i < m_labels.size(); ++i)
    {
        if (i < m_icons.size() && !m_icons.at(i).isNull())
            hasAnyIcon = true;
    }

    const int alignedIconSlotW = hasAnyIcon ? (kTabIconSize + kTabIconGap) : 0;

    // Tab labels
    for (int i = 0; i < m_labels.size(); ++i)
    {
        const TabGeom g = tabGeomAt(i);
        const QRect tabRect =
            m_orientation == Orientation::Vertical
                ? QRect(0, g.x, qMax(1, W - kVerticalDividerW - kIndicatorH), g.width)
                : QRect(g.x, 0, g.width, H - kSepH - kIndicatorH);

        const bool isActive  = (i == m_currentIndex);
        const bool isHovered = (i == m_hoveredIndex) && !isActive;

        if (isActive || isHovered)
        {
            const QRect bgRect =
                m_orientation == Orientation::Vertical
                    ? tabRect.adjusted(6, 6, -10, -6)
                    : tabRect.adjusted(6, 4, -6, -4);
            painter.setBrush(isActive ? pal.tabActiveBackground : pal.tabHoverBackground);
            painter.drawRoundedRect(bgRect, 8.0, 8.0);
        }

        QColor textColor = pal.tabTextIdle;
        if (isActive)
            textColor = pal.tabTextActive;
        else if (isHovered)
            textColor = pal.tabTextHover;

        const int textW = fm.horizontalAdvance(m_labels.at(i));
        const bool hasIcon = i < m_icons.size() && !m_icons.at(i).isNull();
        const int iconSize = hasIcon ? qMin(kTabIconSize, qMax(12, tabRect.height() - 14)) : 0;
        const int iconSlotW =
            (m_orientation == Orientation::Vertical)
                ? alignedIconSlotW
                : (hasIcon ? iconSize + kTabIconGap : 0);

        const int contentW = iconSlotW + textW;
        int contentX = 0;
        if (m_orientation == Orientation::Vertical)
        {
            // File-manager style: fixed left inset + fixed icon column.
            contentX = tabRect.x() + kVerticalContentInsetL;
        }
        else
        {
            contentX = tabRect.x() + (tabRect.width() - contentW) / 2;
        }

        painter.setPen(textColor);
        painter.setFont(font());

        if (hasIcon)
        {
            const int iconX =
                (m_orientation == Orientation::Vertical)
                    ? contentX + qMax(0, (kTabIconSize - iconSize) / 2)
                    : contentX;
            const QRect iconRect(iconX, tabRect.y() + (tabRect.height() - iconSize) / 2, iconSize, iconSize);
            const qreal dpr = painter.device()->devicePixelRatioF();
            const QPixmap pix = tintedIconPixmap(m_icons.at(i), QSize(iconSize, iconSize), textColor, dpr);
            if (!pix.isNull())
            {
                painter.drawPixmap(iconRect, pix);
            }
            else
            {
                m_icons.at(i).paint(&painter, iconRect, Qt::AlignCenter, QIcon::Normal, QIcon::Off);
            }
        }

        contentX += iconSlotW;

        if (m_orientation == Orientation::Vertical)
        {
            const int textAvailW = qMax(0, tabRect.right() - kVerticalContentInsetR - contentX + 1);
            const QString elided = fm.elidedText(m_labels.at(i), Qt::ElideRight, textAvailW);
            const QRect textRect(contentX, tabRect.y(), textAvailW, tabRect.height());
            painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, elided);
        }
        else
        {
            const QRect textRect(contentX, tabRect.y(), textW, tabRect.height());
            painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, m_labels.at(i));
        }
    }

    // Active indicator
    painter.setPen(Qt::NoPen);
    painter.setBrush(pal.tabIndicator);
    if (m_orientation == Orientation::Vertical)
    {
        const int indX = W - kVerticalDividerW - kIndicatorH;
        painter.drawRoundedRect(QRectF(indX, m_indicatorX, kIndicatorH, m_indicatorW), 1.0, 1.0);
    }
    else
    {
        const int indY = H - kSepH - kIndicatorH;
        painter.drawRoundedRect(QRectF(m_indicatorX, indY, m_indicatorW, kIndicatorH), 1.0, 1.0);
    }
}

void EraTabBar::mousePressEvent(QMouseEvent* event)
{
    const qreal pos = m_orientation == Orientation::Vertical
        ? event->position().y()
        : event->position().x();
    const int idx = tabAtPos(static_cast<int>(pos));
    if (idx >= 0)
        setCurrentIndex(idx);
    QWidget::mousePressEvent(event);
}

void EraTabBar::mouseMoveEvent(QMouseEvent* event)
{
    const qreal pos = m_orientation == Orientation::Vertical
        ? event->position().y()
        : event->position().x();
    const int idx = tabAtPos(static_cast<int>(pos));
    if (idx != m_hoveredIndex)
    {
        m_hoveredIndex = idx;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void EraTabBar::leaveEvent(QEvent* event)
{
    if (m_hoveredIndex != -1)
    {
        m_hoveredIndex = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

void EraTabBar::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);

    if (m_orientation == Orientation::Vertical)
    {
        applyOrientationGeometry();
        updateGeometry();
    }

    if (!m_labels.isEmpty())
    {
        const TabGeom g = tabGeomAt(m_currentIndex);
        m_indicatorX = g.x;
        m_indicatorW = g.width;
        m_targetX    = g.x;
        m_targetW    = g.width;
    }
    update();
}

void EraTabBar::init()
{
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    applyOrientationGeometry();

    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(kAnimMs);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        const qreal t = v.toReal();
        m_indicatorX = m_animStartX + (m_targetX - m_animStartX) * t;
        m_indicatorW = m_animStartW + (m_targetW - m_animStartW) * t;
        update();
    });
}

void EraTabBar::applyOrientationGeometry()
{
    if (m_orientation == Orientation::Vertical)
    {
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setFixedWidth(sizeHint().width());
    }
    else
    {
        setMinimumWidth(0);
        setMaximumWidth(QWIDGETSIZE_MAX);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(kHorizontalBarHeight);
    }
}

int EraTabBar::verticalTabHeight() const
{
    const QFontMetrics fm(font());
    return qMax(kVerticalTabMinHeight, qMax(fm.height() + kVerticalTabExtraPadding, kTabIconSize + 16));
}

int EraTabBar::tabContentWidthAt(int index, const QFontMetrics& fm) const
{
    if (index < 0 || index >= m_labels.size())
        return 0;

    const int textW = fm.horizontalAdvance(m_labels.at(index));
    const bool hasIcon = index < m_icons.size() && !m_icons.at(index).isNull();
    const int iconW = hasIcon ? (kTabIconSize + kTabIconGap) : 0;
    return textW + iconW;
}

EraTabBar::TabGeom EraTabBar::tabGeomAt(int index) const
{
    if (m_orientation == Orientation::Vertical)
    {
        const int tabH = verticalTabHeight();
        int y = 0;
        for (int i = 0; i < m_labels.size(); ++i)
        {
            if (i == index)
                return {y, tabH};
            y += tabH;
        }
        return {0, 0};
    }

    const QFontMetrics fm(font());
    int x = 0;
    for (int i = 0; i < m_labels.size(); ++i)
    {
        const int w = tabContentWidthAt(i, fm) + kTabPaddingH * 2;
        if (i == index)
            return {x, w};
        x += w;
    }
    return {0, 0};
}

int EraTabBar::tabAtPos(int px) const
{
    if (m_orientation == Orientation::Vertical)
    {
        const int tabH = verticalTabHeight();
        int y = 0;
        for (int i = 0; i < m_labels.size(); ++i)
        {
            if (px >= y && px < y + tabH)
                return i;
            y += tabH;
        }
        return -1;
    }

    const QFontMetrics fm(font());
    int x = 0;
    for (int i = 0; i < m_labels.size(); ++i)
    {
        const int w = tabContentWidthAt(i, fm) + kTabPaddingH * 2;
        if (px >= x && px < x + w)
            return i;
        x += w;
    }
    return -1;
}

void EraTabBar::animateIndicatorTo(int index)
{
    const TabGeom g = tabGeomAt(index);
    m_targetX = g.x;
    m_targetW = g.width;

    if (m_anim->state() == QAbstractAnimation::Running)
        m_anim->stop();

    m_animStartX = m_indicatorX;
    m_animStartW = m_indicatorW;

    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->start();
}
