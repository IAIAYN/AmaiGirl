#pragma once

#include <QPlainTextEdit>
#include <QColor>

class QEnterEvent;
class QFocusEvent;

class EraPlainTextEdit : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit EraPlainTextEdit(QWidget* parent = nullptr);
    explicit EraPlainTextEdit(const QString& text, QWidget* parent = nullptr);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void init();
    void updateColors();

    bool m_hovered{false};
    bool m_updatingColors{false};
    QString m_lastStyleSheet;
    QColor m_borderColor;
    QColor m_placeholderColor;
    QColor m_textColor;
};
