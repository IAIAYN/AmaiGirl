#pragma once

#include <QWidget>
#include <QStringList>
#include <QIcon>
#include <QVector>

class QVariantAnimation;
class QEnterEvent;
class QEvent;
class QMouseEvent;
class QPaintEvent;
class QFontMetrics;

class EraTabBar : public QWidget
{
    Q_OBJECT
public:
    enum class Orientation
    {
        Horizontal,
        Vertical
    };

    explicit EraTabBar(QWidget* parent = nullptr);

    void addTab(const QString& label);
    void addTab(const QString& label, const QIcon& icon);
    void setTabText(int index, const QString& label);
    void setTabIcon(int index, const QIcon& icon);
    void setCurrentIndex(int index);
    void setOrientation(Orientation orientation);
    Orientation orientation() const { return m_orientation; }
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
    void applyOrientationGeometry();
    int verticalTabHeight() const;
    int tabContentWidthAt(int index, const QFontMetrics& fm) const;
    TabGeom tabGeomAt(int index) const;
    int tabAtPos(int px) const;
    void animateIndicatorTo(int index);

    QStringList m_labels;
    QVector<QIcon> m_icons;
    int m_currentIndex{0};
    int m_hoveredIndex{-1};

    qreal m_indicatorX{0.0};
    qreal m_indicatorW{0.0};

    qreal m_targetX{0.0};
    qreal m_targetW{0.0};

    QVariantAnimation* m_anim{nullptr};
    qreal m_animStartX{0.0};
    qreal m_animStartW{0.0};
    Orientation m_orientation{Orientation::Horizontal};
};
