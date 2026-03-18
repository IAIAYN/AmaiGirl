#pragma once

#include <QComboBox>
#include <QColor>

class QListView;
class QVariantAnimation;
class QEnterEvent;
class QPaintEvent;
class QShowEvent;
class QEvent;

class EraComboBox : public QComboBox
{
    Q_OBJECT
public:
    explicit EraComboBox(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void showPopup() override;
    void changeEvent(QEvent* event) override;

private:
    void init();
    void updateColors(bool animated);
    void animateTo(const QColor& border, bool animated);
    void refreshPopupStyle();
    static QColor blend(const QColor& from, const QColor& to, qreal t);

    bool m_hovered{false};
    bool m_refreshingPopupStyle{false};
    QString m_lastPopupStyleSheet;
    QColor m_borderColor;
    QColor m_targetBorderColor;
    QVariantAnimation* m_anim{nullptr};
    QColor m_animStartBorder;
};
