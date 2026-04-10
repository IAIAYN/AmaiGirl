#pragma once

#include <QWidget>

class QTimer;

class TypingDotsWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit TypingDotsWidget(QWidget* parent = nullptr);

    void setActive(bool active);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QTimer* m_timer{nullptr};
    int m_phase{0};
};
