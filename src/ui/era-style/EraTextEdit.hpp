#pragma once

#include <QTextEdit>

class QEnterEvent;
class QFocusEvent;
class QKeyEvent;
class QPaintEvent;

class EraTextEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit EraTextEdit(QWidget* parent = nullptr);

Q_SIGNALS:
    void sendRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void updateColors();

    bool m_hovered{false};
    bool m_updatingColors{false};
    QString m_lastStyleSheet;
    QColor m_borderColor;
    QColor m_placeholderColor;
    QColor m_textColor;
};
