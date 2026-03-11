#pragma once

#include <QWidget>
#include <QStringList>

class QVariantAnimation;
class QEnterEvent;
class QEvent;
class QMouseEvent;
class QPaintEvent;

class EraTabBar : public QWidget
{
    Q_OBJECT
public:
    explicit EraTabBar(QWidget* parent = nullptr);

    void addTab(const QString& label);
    void setTabText(int index, const QString& label);
    void setCurrentIndex(int index);
    int currentIndex() const { return m_currentIndex; }
    int count() const { return m_labels.size(); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void currentChanged(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    struct TabGeom
    {
        int x{0};
        int width{0};
    };

    void init();
    TabGeom tabGeomAt(int index) const;
    int tabAtPos(int px) const;
    void animateIndicatorTo(int index);

    QStringList m_labels;
    int m_currentIndex{0};
    int m_hoveredIndex{-1};

    qreal m_indicatorX{0.0};
    qreal m_indicatorW{0.0};

    qreal m_targetX{0.0};
    qreal m_targetW{0.0};

    QVariantAnimation* m_anim{nullptr};
    qreal m_animStartX{0.0};
    qreal m_animStartW{0.0};
};
