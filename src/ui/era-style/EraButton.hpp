#pragma once

#include <QPushButton>
#include <QColor>

class QVariantAnimation;
class QEnterEvent;
class QEvent;
class QMouseEvent;
class QPaintEvent;

class EraButton : public QPushButton
{
    Q_OBJECT
public:
    enum class Tone
    {
        Neutral,
        Link,
        Success,
        Warning,
        Danger,
        Info
    };

    explicit EraButton(const QString& text, QWidget* parent = nullptr);
    explicit EraButton(QWidget* parent = nullptr);

    void setTone(Tone tone);
    Tone tone() const { return m_tone; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    struct Palette
    {
        QColor borderDefault;
        QColor borderHover;
        QColor borderPressed;
        QColor textDefault;
        QColor textHover;
        QColor textPressed;
        QColor textDisabled;
        QColor backgroundDefault;
        QColor backgroundHover;
        QColor backgroundPressed;
    };

    void init();
    void updateTargetState(bool animated);
    void animateTo(const QColor& border, const QColor& text, const QColor& background, bool animated);
    Palette paletteForTone() const;
    static QColor blend(const QColor& from, const QColor& to, qreal t);

    Tone m_tone{Tone::Neutral};
    bool m_hovered{false};
    bool m_pressed{false};

    QColor m_borderColor;
    QColor m_textColor;
    QColor m_backgroundColor;
    QColor m_targetBorderColor;
    QColor m_targetTextColor;
    QColor m_targetBackgroundColor;

    QVariantAnimation* m_anim{nullptr};
    QColor m_animStartBorder;
    QColor m_animStartText;
    QColor m_animStartBackground;
};
