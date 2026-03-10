#pragma once

#include <QCheckBox>
#include <QColor>

class QVariantAnimation;
class QEnterEvent;
class QPaintEvent;
class QMouseEvent;
class QEvent;

class EraSwitch : public QCheckBox
{
    Q_OBJECT
public:
    explicit EraSwitch(const QString& text, QWidget* parent = nullptr);
    explicit EraSwitch(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool hitButton(const QPoint& pos) const override;

private:
    void init();
    void updateTargetState(bool animated);
    void animateTo(qreal thumbT, const QColor& trackColor, bool animated);
    static QColor blend(const QColor& from, const QColor& to, qreal t);

    bool m_hovered{false};
    bool m_pressed{false};
    qreal m_thumbT{0.0};
    qreal m_targetThumbT{0.0};
    QColor m_trackColor;
    QColor m_targetTrackColor;
    QVariantAnimation* m_anim{nullptr};
    qreal m_animStartThumbT{0.0};
    QColor m_animStartTrackColor;
};
