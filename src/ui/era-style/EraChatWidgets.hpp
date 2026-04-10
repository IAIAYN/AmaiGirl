#pragma once

#include <QColor>
#include <QListWidget>
#include <QCheckBox>
#include <QTextEdit>
#include <QTextCursor>
#include <QToolButton>
#include <QWidget>

#include <memory>

class QEnterEvent;
class QKeyEvent;
class QContextMenuEvent;
class QFocusEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;
class QTextDocument;

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
        Danger,
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

class EraChatSelectionCheckBox : public QCheckBox
{
    Q_OBJECT
public:
    explicit EraChatSelectionCheckBox(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;
};

class EraChatBubbleTextView : public QWidget
{
    Q_OBJECT
public:
    explicit EraChatBubbleTextView(bool isUserMessage = false, QWidget* parent = nullptr);

    void setPlainText(const QString& text);
    void appendPlainText(const QString& text);
    QString toPlainText() const;

    void selectAll();
    void copy() const;
    bool hasSelection() const;

    void setUserMessage(bool isUserMessage);
    bool isUserMessage() const { return m_isUserMessage; }
    void setMessageSelectionState(int sourceMessageIndex, bool checked);

    QSize layoutForMaxWidth(int maxWidth);
    QSize measureForMaxWidth(int maxWidth) const;
    QSizeF textSizeForWidth(int width);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

Q_SIGNALS:
    void requestToggleMessageSelection(int sourceMessageIndex, bool checked);

private:
    enum class SelectionGranularity
    {
        Character,
        Word,
        Paragraph
    };

    struct SelectionBounds
    {
        int start{-1};
        int end{-1};

        bool isValid() const { return start >= 0 && end >= start; }
    };

    int hitTestPosition(const QPoint& pos) const;
    void setSelectionRange(int anchor, int position);
    SelectionBounds wordBoundsAtPosition(int position) const;
    SelectionBounds paragraphBoundsAtPosition(int position) const;
    void selectWordAtPosition(int position);
    void selectParagraphAtPosition(int position);
    void trackLeftClick(const QPoint& pos, ulong timestamp, bool isDoubleClick);
    void clearSelection();
    void refreshLayoutWidth();
    void init();
    void refreshAppearance();

    std::unique_ptr<QTextDocument> m_doc;
    int m_anchorPosition{-1};
    int m_cursorPosition{-1};
    bool m_dragSelecting{false};
    bool m_isUserMessage{false};
    bool m_updatingColors{false};
    int m_sourceMessageIndex{-1};
    bool m_messageChecked{false};
    QColor m_textColor;
    QColor m_disabledTextColor;
    QSize m_layoutSize{1, 1};
    QTextOption::WrapMode m_layoutWrapMode{QTextOption::WrapAtWordBoundaryOrAnywhere};
    int m_layoutTextWidth{1};
    QPoint m_lastClickPos;
    ulong m_lastClickTimestamp{0};
    int m_leftClickStreak{0};
    bool m_hasLastClick{false};
    SelectionGranularity m_selectionGranularity{SelectionGranularity::Character};
    SelectionBounds m_selectionAnchorBounds;
};

class EraChatListWidget : public QListWidget
{
public:
    explicit EraChatListWidget(QWidget* parent = nullptr);

protected:
    void changeEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void init();
    void refreshAppearance();
};
