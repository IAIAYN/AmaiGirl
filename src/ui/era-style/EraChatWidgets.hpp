#pragma once

#include <QListWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QToolButton>

class QEnterEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

class EraChatComposerEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit EraChatComposerEdit(QWidget* parent = nullptr);

    int preferredHeight(int minLines, int maxLines) const;
    int documentHeight() const;

Q_SIGNALS:
    void sendRequested();
    void metricsChanged();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void init();
    void refreshAppearance();

    bool m_updatingColors{false};
};

class EraIconToolButton : public QToolButton
{
public:
    enum class Tone
    {
        Accent,
        Ghost
    };

    explicit EraIconToolButton(QWidget* parent = nullptr);

    void setTone(Tone tone);
    Tone tone() const { return m_tone; }

    void setIconLogicalSize(int size);
    int iconLogicalSize() const { return m_iconLogicalSize; }

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
    void init();

    Tone m_tone{Tone::Ghost};
    bool m_hovered{false};
    bool m_pressed{false};
    int m_iconLogicalSize{16};
};

class EraChatBubbleBox : public QWidget
{
public:
    explicit EraChatBubbleBox(bool isUserBubble = false, QWidget* parent = nullptr);

    void setUserBubble(bool isUserBubble);
    bool isUserBubble() const { return m_isUserBubble; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    bool m_isUserBubble{false};
};

class EraChatBubbleTextView : public QTextBrowser
{
public:
    explicit EraChatBubbleTextView(bool isUserMessage = false, QWidget* parent = nullptr);

    void setUserMessage(bool isUserMessage);
    bool isUserMessage() const { return m_isUserMessage; }

protected:
    void changeEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void init();
    void refreshAppearance();

    bool m_isUserMessage{false};
    bool m_updatingColors{false};
};

class EraChatListWidget : public QListWidget
{
public:
    explicit EraChatListWidget(QWidget* parent = nullptr);

protected:
    void changeEvent(QEvent* event) override;

private:
    void init();
    void refreshAppearance();
};
