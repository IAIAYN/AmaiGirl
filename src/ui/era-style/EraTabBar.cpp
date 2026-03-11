#include "ui/era-style/EraTabBar.hpp"
#include "ui/era-style/EraStyleColor.hpp"

#include <QVariantAnimation>
#include <QPainter>
#include <QPaintEvent>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QEasingCurve>

namespace {
constexpr int kTabPaddingH = 14;
constexpr int kBarHeight   = 38;
constexpr int kIndicatorH  = 2;
constexpr int kSepH        = 1;
constexpr int kAnimMs      = 330;
}

EraTabBar::EraTabBar(QWidget* parent)
    : QWidget(parent)
{
    init();
}

void EraTabBar::addTab(const QString& label)
{
    m_labels.append(label);

    if (m_labels.size() == 1)
    {
        const TabGeom g = tabGeomAt(0);
        m_indicatorX = g.x;
        m_indicatorW = g.width;
        m_targetX    = g.x;
        m_targetW    = g.width;
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

QSize EraTabBar::sizeHint() const
{
    int totalW = 0;
    const QFontMetrics fm(font());
    for (const QString& label : m_labels)
        totalW += fm.horizontalAdvance(label) + kTabPaddingH * 2;
    return QSize(qMax(totalW, 80), kBarHeight);
}

QSize EraTabBar::minimumSizeHint() const
{
    return QSize(80, kBarHeight);
}

void EraTabBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int W = width();
    const int H = height();

    // 分割线
    const int sepY = H - kSepH;
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().color(QPalette::Mid));
    painter.drawRect(0, sepY, W, kSepH);

    // Tab 文字
    for (int i = 0; i < m_labels.size(); ++i)
    {
        const TabGeom g = tabGeomAt(i);
        const QRect tabRect(g.x, 0, g.width, H - kSepH - kIndicatorH);

        const bool isActive  = (i == m_currentIndex);
        const bool isHovered = (i == m_hoveredIndex) && !isActive;

        QColor textColor;
        if (isActive)
            textColor = EraStyleColor::Link;
        else if (isHovered)
            textColor = palette().color(QPalette::ButtonText);
        else
            textColor = palette().color(QPalette::PlaceholderText);

        painter.setPen(textColor);
        painter.setFont(font());
        painter.drawText(tabRect, Qt::AlignCenter, m_labels.at(i));
    }

    // 下标指示线
    const int indY = H - kSepH - kIndicatorH;
    painter.setPen(Qt::NoPen);
    painter.setBrush(EraStyleColor::Link);
    painter.drawRoundedRect(QRectF(m_indicatorX, indY, m_indicatorW, kIndicatorH), 1.0, 1.0);
}

void EraTabBar::mousePressEvent(QMouseEvent* event)
{
    const int idx = tabAtPos(event->position().x());
    if (idx >= 0)
        setCurrentIndex(idx);
    QWidget::mousePressEvent(event);
}

void EraTabBar::mouseMoveEvent(QMouseEvent* event)
{
    const int idx = tabAtPos(event->position().x());
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
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kBarHeight);
    setCursor(Qt::PointingHandCursor);

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

EraTabBar::TabGeom EraTabBar::tabGeomAt(int index) const
{
    const QFontMetrics fm(font());
    int x = 0;
    for (int i = 0; i < m_labels.size(); ++i)
    {
        const int w = fm.horizontalAdvance(m_labels.at(i)) + kTabPaddingH * 2;
        if (i == index)
            return {x, w};
        x += w;
    }
    return {0, 0};
}

int EraTabBar::tabAtPos(int px) const
{
    const QFontMetrics fm(font());
    int x = 0;
    for (int i = 0; i < m_labels.size(); ++i)
    {
        const int w = fm.horizontalAdvance(m_labels.at(i)) + kTabPaddingH * 2;
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
