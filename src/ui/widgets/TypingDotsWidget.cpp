#include "ui/widgets/TypingDotsWidget.hpp"

#include <QPainter>
#include <QTimer>

TypingDotsWidget::TypingDotsWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(30, 12);
    m_timer = new QTimer(this);
    m_timer->setInterval(220);
    connect(m_timer, &QTimer::timeout, this, [this] {
        m_phase = (m_phase + 1) % 3;
        update();
    });
}

void TypingDotsWidget::setActive(bool active)
{
    if (active)
    {
        if (!m_timer->isActive())
            m_timer->start();
        return;
    }

    if (m_timer->isActive())
        m_timer->stop();
    m_phase = 0;
    update();
}

void TypingDotsWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor base = palette().color(QPalette::Text);
    const qreal radius = 2.2;
    const qreal centerY = height() * 0.5;
    const qreal startX = 7.0;
    const qreal step = 8.0;

    for (int i = 0; i < 3; ++i)
    {
        QColor c = base;
        c.setAlphaF(i == m_phase ? 0.95 : 0.32);
        painter.setPen(Qt::NoPen);
        painter.setBrush(c);
        const qreal cx = startX + i * step;
        painter.drawEllipse(QPointF(cx, centerY), radius, radius);
    }
}
