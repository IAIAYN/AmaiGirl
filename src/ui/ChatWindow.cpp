#include "ui/ChatWindow.hpp"

#include "common/SettingsManager.hpp"
#include "common/Utils.hpp"
#include "ai/ConversationRepository.hpp"

#include <QBoxLayout>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProcessEnvironment>
#include <QDebug>
#include <QScrollBar>
#include <QScrollArea>
#include <QScreen>
#include <QSignalBlocker>
#include <QSet>
#include <QTimer>
#include <QTextEdit>
#include <QVBoxLayout>

#include "ui/theme/ThemeApi.hpp"
#include "ui/theme/ThemeWidgets.hpp"
#include "ui/widgets/TypingDotsWidget.hpp"

namespace {

constexpr int kComposerMinLines = 2;
constexpr int kComposerMaxLines = 5;
constexpr int kComposerRadius = 18;
constexpr int kComposerSendButtonSize = 34;
constexpr int kComposerSendIconSize = 18;
constexpr int kComposerFooterControlSize = 24;
constexpr int kComposerClearIconSize = 19;
constexpr qreal kComposerInputDocumentMargin = 3.0;
constexpr int kMcpPopupWidth = 340;
constexpr int kMcpPopupMaxHeight = 320;
constexpr int kBubbleInnerPadX = 12;
constexpr int kBubbleInnerPadY = 8;
constexpr int kMessageSelectCheckBoxSize = 18;
constexpr int kMessageSelectSlotWidth = 26;
constexpr int kMessageSelectSlotHeight = 32;

QPixmap circleAvatar(const QPixmap& src, int logicalSize, qreal devicePixelRatio)
{
    if (src.isNull()) return {};

    const int px = qMax(1, int(std::round(logicalSize * devicePixelRatio)));

    // Scale in device pixels to avoid blur on Retina.
    QPixmap scaled = src.scaled(px, px, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

    QPixmap out(px, px);
    out.fill(Qt::transparent);

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addEllipse(0, 0, px, px);
    p.setClipPath(clip);
    p.drawPixmap(0, 0, scaled);
    p.end();

    out.setDevicePixelRatio(devicePixelRatio);
    return out;
}

QString firstPngInDir(const QString& dir)
{
    QDir d(dir);
    const auto files = d.entryInfoList(QStringList{QStringLiteral("*.png"), QStringLiteral("*.PNG")}, QDir::Files, QDir::Name);
    if (files.isEmpty()) return {};
    return files.front().absoluteFilePath();
}

bool hasVisibleCharacters(const QString& text)
{
    for (const QChar ch : text)
    {
        if (!ch.isSpace())
            return true;
    }
    return false;
}

bool chatLayoutDebugEnabled()
{
    static const bool enabled =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("AMAI_CHAT_LAYOUT_DEBUG")) == QStringLiteral("1");
    return enabled;
}

QString mcpStatusText(McpServerRuntimeState state)
{
    switch (state)
    {
    case McpServerRuntimeState::Disabled:
        return QCoreApplication::translate("ChatWindow", "已关闭");
    case McpServerRuntimeState::Starting:
        return QCoreApplication::translate("ChatWindow", "正在启动");
    case McpServerRuntimeState::Enabled:
        return QCoreApplication::translate("ChatWindow", "已启用");
    case McpServerRuntimeState::Unavailable:
        return QCoreApplication::translate("ChatWindow", "无法使用");
    }

    return QString();
}

class McpStatusIndicator final : public QWidget
{
    Q_OBJECT
public:
    explicit McpStatusIndicator(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(12, 12);
    }

    void setState(McpServerRuntimeState state)
    {
        if (m_state == state)
            return;
        m_state = state;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QColor fill;
        switch (m_state)
        {
        case McpServerRuntimeState::Disabled:
            fill = QColor(0x9a, 0xa6, 0xb7);
            break;
        case McpServerRuntimeState::Starting:
            fill = QColor(0xf0, 0xa0, 0x28);
            break;
        case McpServerRuntimeState::Enabled:
            fill = QColor(0x33, 0xb8, 0x69);
            break;
        case McpServerRuntimeState::Unavailable:
            fill = QColor(0xe3, 0x54, 0x4a);
            break;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawEllipse(rect().adjusted(1, 1, -1, -1));
    }

private:
    McpServerRuntimeState m_state{McpServerRuntimeState::Disabled};
};

class McpServerToggleRow final : public QWidget
{
    Q_OBJECT
public:
    explicit McpServerToggleRow(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("chatMcpPopupRow"));
        setAttribute(Qt::WA_StyledBackground, true);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 8, 10, 8);
        layout->setSpacing(10);

        m_indicator = new McpStatusIndicator(this);
        layout->addWidget(m_indicator, 0, Qt::AlignLeft | Qt::AlignVCenter);

        auto* textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(2);

        m_nameLabel = new QLabel(this);
        m_nameLabel->setObjectName(QStringLiteral("chatMcpPopupRowName"));
        textLayout->addWidget(m_nameLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);

        m_statusLabel = new QLabel(this);
        m_statusLabel->setObjectName(QStringLiteral("chatMcpPopupRowStatus"));
        textLayout->addWidget(m_statusLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);

        layout->addLayout(textLayout, 1);

        m_switch = new ThemeWidgets::Switch(this);
        m_switch->setText(QString());
        layout->addWidget(m_switch, 0, Qt::AlignRight | Qt::AlignVCenter);

        connect(m_switch, &QCheckBox::toggled, this, [this](bool checked) {
            emit enabledChanged(m_status.name, checked);
        });
    }

    void setStatus(const McpServerStatus& status)
    {
        m_status = status;
        if (m_indicator)
            m_indicator->setState(status.state);
        if (m_nameLabel)
            m_nameLabel->setText(status.name);
        if (m_statusLabel)
            m_statusLabel->setText(mcpStatusText(status.state));
        if (m_switch)
        {
            const QSignalBlocker blocker(m_switch);
            m_switch->setChecked(status.enabled);
            m_switch->setEnabled(status.state != McpServerRuntimeState::Starting);
        }

        const QString detail = status.detail.trimmed();
        setToolTip(detail);
        if (m_statusLabel)
            m_statusLabel->setToolTip(detail);
        if (m_nameLabel)
            m_nameLabel->setToolTip(detail);
    }

Q_SIGNALS:
    void enabledChanged(const QString& serverName, bool enabled);

private:
    McpServerStatus m_status;
    McpStatusIndicator* m_indicator{nullptr};
    QLabel* m_nameLabel{nullptr};
    QLabel* m_statusLabel{nullptr};
    ThemeWidgets::Switch* m_switch{nullptr};
};

class McpServerPopup final : public QFrame
{
    Q_OBJECT
public:
    explicit McpServerPopup(QWidget* parent = nullptr)
        : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
    {
        setObjectName(QStringLiteral("chatMcpPopup"));
        setAttribute(Qt::WA_StyledBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        m_card = new QWidget(this);
        m_card->setObjectName(QStringLiteral("chatMcpPopupCard"));
        m_card->setAttribute(Qt::WA_StyledBackground, true);
        root->addWidget(m_card);

        auto* cardLayout = new QVBoxLayout(m_card);
        cardLayout->setContentsMargins(12, 12, 12, 12);
        cardLayout->setSpacing(10);

        m_titleLabel = new QLabel(m_card);
        m_titleLabel->setObjectName(QStringLiteral("chatMcpPopupTitle"));
        cardLayout->addWidget(m_titleLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);

        m_scrollArea = new QScrollArea(m_card);
        m_scrollArea->setObjectName(QStringLiteral("chatMcpPopupScroll"));
        m_scrollArea->setWidgetResizable(true);
        m_scrollArea->setFrameShape(QFrame::NoFrame);
        m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        Theme::installHoverScrollBars(m_scrollArea, true, false);
        cardLayout->addWidget(m_scrollArea, 1);

        m_contentWidget = new QWidget(m_scrollArea);
        m_contentWidget->setObjectName(QStringLiteral("chatMcpPopupContent"));
        m_scrollArea->setWidget(m_contentWidget);

        m_rowsLayout = new QVBoxLayout(m_contentWidget);
        m_rowsLayout->setContentsMargins(0, 0, 0, 0);
        m_rowsLayout->setSpacing(8);

        refreshTexts();
        resize(kMcpPopupWidth, 180);
    }

    void setStatuses(const QList<McpServerStatus>& statuses)
    {
        m_statuses = statuses;
        rebuildRows();
    }

    void popupNearAnchor(QWidget* anchor)
    {
        if (!anchor)
            return;

        adjustSize();
        QSize targetSize = sizeHint();
        targetSize.setWidth(qMax(targetSize.width(), kMcpPopupWidth));
        targetSize.setHeight(qMin(targetSize.height(), kMcpPopupMaxHeight));
        resize(targetSize);

        QPoint pos = anchor->mapToGlobal(QPoint(0, -height() - 8));
        if (QScreen* screen = QGuiApplication::screenAt(pos))
        {
            const QRect avail = screen->availableGeometry();
            if (pos.x() + width() > avail.right())
                pos.setX(qMax(avail.left(), avail.right() - width()));
            if (pos.x() < avail.left())
                pos.setX(avail.left());
            if (pos.y() < avail.top())
                pos.setY(qMin(avail.bottom() - height(), anchor->mapToGlobal(QPoint(0, anchor->height() + 8)).y()));
        }

        move(pos);
        show();
        raise();
        activateWindow();
    }

protected:
    void changeEvent(QEvent* event) override
    {
        QFrame::changeEvent(event);
        if (!event)
            return;

        if (event->type() == QEvent::LanguageChange)
        {
            refreshTexts();
            rebuildRows();
        }
    }

private:
    void refreshTexts()
    {
        if (m_titleLabel)
            m_titleLabel->setText(QCoreApplication::translate("ChatWindow", "MCP 列表"));
    }

    void rebuildRows()
    {
        if (!m_rowsLayout)
            return;

        while (QLayoutItem* item = m_rowsLayout->takeAt(0))
        {
            if (QWidget* widget = item->widget())
                widget->deleteLater();
            delete item;
        }

        if (m_statuses.isEmpty())
        {
            auto* emptyLabel = new QLabel(QCoreApplication::translate("ChatWindow", "暂无 MCP 服务器"), m_contentWidget);
            emptyLabel->setObjectName(QStringLiteral("chatMcpPopupEmptyLabel"));
            emptyLabel->setWordWrap(true);
            m_rowsLayout->addWidget(emptyLabel, 0, Qt::AlignLeft | Qt::AlignTop);
            m_rowsLayout->addStretch(1);
            return;
        }

        for (const McpServerStatus& status : m_statuses)
        {
            auto* row = new McpServerToggleRow(m_contentWidget);
            row->setStatus(status);
            connect(row, &McpServerToggleRow::enabledChanged, this, &McpServerPopup::enabledChanged);
            m_rowsLayout->addWidget(row);
        }
        m_rowsLayout->addStretch(1);
    }

Q_SIGNALS:
    void enabledChanged(const QString& serverName, bool enabled);

private:
    QWidget* m_card{nullptr};
    QLabel* m_titleLabel{nullptr};
    QScrollArea* m_scrollArea{nullptr};
    QWidget* m_contentWidget{nullptr};
    QVBoxLayout* m_rowsLayout{nullptr};
    QList<McpServerStatus> m_statuses;
};

class ChatMessageWidget final : public QWidget
{
    Q_OBJECT
public:
    ChatMessageWidget(const QPixmap& avatar, const QString& text, bool isUser, QWidget* parent=nullptr)
        : QWidget(parent), m_isUser(isUser)
    {
        auto lay = new QHBoxLayout(this);
        lay->setContentsMargins(10, 8, 10, 8);
        lay->setSpacing(10);

        m_avatar = new QLabel(this);
        m_avatar->setFixedSize(32, 32);
        setAvatar(avatar);

        m_selectSlot = new QWidget(this);
        m_selectSlot->setFixedSize(kMessageSelectSlotWidth, kMessageSelectSlotHeight);
        auto* selectSlotLayout = new QVBoxLayout(m_selectSlot);
        selectSlotLayout->setContentsMargins(4, 0, 4, 0);
        selectSlotLayout->setSpacing(0);

        m_selectCheck = new ThemeWidgets::ChatSelectionCheckBox(m_selectSlot);
        m_selectCheck->setVisible(false);
        m_selectCheck->setFocusPolicy(Qt::NoFocus);
        m_selectCheck->setFixedSize(kMessageSelectCheckBoxSize, kMessageSelectCheckBoxSize);
        m_selectSlot->setVisible(false);
        m_selectSlot->setFixedWidth(0);
        selectSlotLayout->addWidget(m_selectCheck, 0, Qt::AlignHCenter | Qt::AlignVCenter);

        m_bubbleBox = new ThemeWidgets::ChatBubbleBox(m_isUser, this);
        auto* bubbleLay = new QVBoxLayout(m_bubbleBox);
        bubbleLay->setContentsMargins(kBubbleInnerPadX, kBubbleInnerPadY, kBubbleInnerPadX, kBubbleInnerPadY);
        bubbleLay->setSpacing(0);

        m_text = new ThemeWidgets::ChatBubbleTextView(m_isUser, m_bubbleBox);
        m_text->setPlainText(text);
        updateBubbleTextStyle();

        m_typingDots = new TypingDotsWidget(this);
        m_typingDots->setVisible(false);

        bubbleLay->addWidget(m_text);
        m_bubbleBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        // Keep selection checkbox in the same left column for both user and assistant rows.
        lay->addWidget(m_selectSlot, 0, Qt::AlignLeft | Qt::AlignTop);

        if (m_isUser)
        {
            lay->addStretch(1);
            lay->addWidget(m_bubbleBox, 0, Qt::AlignRight | Qt::AlignTop);
            lay->addWidget(m_avatar, 0, Qt::AlignRight | Qt::AlignTop);
        }
        else
        {
            lay->addWidget(m_avatar, 0, Qt::AlignLeft | Qt::AlignTop);
            lay->addWidget(m_bubbleBox, 0, Qt::AlignLeft | Qt::AlignTop);
            lay->addWidget(m_typingDots, 0, Qt::AlignLeft | Qt::AlignVCenter);
            lay->addStretch(1);
        }

        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

        // Initial sizing
        syncTextSizeToContent();

        connect(m_text, &ThemeWidgets::ChatBubbleTextView::requestToggleMessageSelection, this,
                [this](int sourceMessageIndex, bool checked) {
            setMessageChecked(checked);
            emit messageSelectionToggled(sourceMessageIndex, checked);
        });

        connect(m_selectCheck, &QCheckBox::toggled, this, [this](bool checked) {
            m_checked = checked;
            if (m_sourceMessageIndex >= 0)
                emit messageSelectionToggled(m_sourceMessageIndex, checked);
        });
    }

    void setAvatar(const QPixmap& avatar)
    {
        const qreal dpr = devicePixelRatioF();
        m_avatar->setPixmap(circleAvatar(avatar, 32, dpr));
    }

    void updateBubbleTextStyle()
    {
        if (m_bubbleBox)
            m_bubbleBox->setUserBubble(m_isUser);
        if (m_text)
            m_text->setUserMessage(m_isUser);
    }

    void setMaxBubbleWidth(int w)
    {
        m_maxBubbleWidth = qMax(1, w);
        syncTextSizeToContent();
        updateGeometry();
    }

    void setRowWidth(int w)
    {
        m_rowWidth = qMax(1, w);
        updateGeometry();
    }

    void appendToken(const QString& t)
    {
        if (m_waitingForResponse && hasVisibleCharacters(t))
            setWaitingForResponse(false);

        m_text->appendPlainText(t);
        updateBubbleTextStyle();
        syncTextSizeToContent();
        updateGeometry();
        update();
    }

    void setContent(const QString& c)
    {
        if (m_waitingForResponse && hasVisibleCharacters(c))
            setWaitingForResponse(false);

        m_text->setPlainText(c);
        updateBubbleTextStyle();
        syncTextSizeToContent();
        updateGeometry();
        update();
    }

    void setWaitingForResponse(bool waiting)
    {
        const bool normalized = waiting && !m_isUser;
        if (m_waitingForResponse == normalized)
            return;

        m_waitingForResponse = normalized;
        if (m_typingDots) {
            m_typingDots->setActive(m_waitingForResponse);
            m_typingDots->setVisible(m_waitingForResponse);
        }
        if (m_bubbleBox)
            m_bubbleBox->setVisible(!m_waitingForResponse);
        if (m_text)
            m_text->setVisible(!m_waitingForResponse);

        updateGeometry();
        update();
    }

    QString content() const
    {
        return m_text ? m_text->toPlainText() : QString();
    }

    void setSourceMessageIndex(int index)
    {
        m_sourceMessageIndex = index;
        if (m_text)
            m_text->setMessageSelectionState(m_sourceMessageIndex, m_checked);
        if (m_sourceMessageIndex < 0)
            setSelectionModeEnabled(false);
    }

    int sourceMessageIndex() const { return m_sourceMessageIndex; }

    void setSelectionModeEnabled(bool enabled)
    {
        const bool visible = enabled && m_sourceMessageIndex >= 0;
        if (m_selectSlot)
        {
            m_selectSlot->setVisible(visible);
            m_selectSlot->setFixedWidth(visible ? kMessageSelectSlotWidth : 0);
        }
        if (m_selectCheck)
            m_selectCheck->setVisible(visible);
        updateGeometry();
    }

    void setMessageChecked(bool checked)
    {
        m_checked = checked;
        if (m_text)
            m_text->setMessageSelectionState(m_sourceMessageIndex, checked);
        if (!m_selectCheck)
            return;
        const QSignalBlocker blocker(m_selectCheck);
        m_selectCheck->setChecked(checked);
    }

    bool isMessageChecked() const { return m_checked; }

    QSize sizeHint() const override
    {
        // Height = max(avatar, visible content) + paddings.
        const int rowTopBottom = 16;
        const int contentH = m_waitingForResponse ? m_waitingContentHeight : m_bubbleContentHeight;

        const int h = qMax(32, contentH) + rowTopBottom;
        return { 0, h };
    }

Q_SIGNALS:
    void messageSelectionToggled(int sourceMessageIndex, bool checked);

private:
    void syncTextSizeToContent()
    {
        if (!m_text) return;

        if (m_waitingForResponse)
        {
            m_text->setVisible(false);
            if (m_typingDots)
            {
                m_typingDots->setVisible(true);
                m_waitingContentHeight = m_typingDots->sizeHint().height();
            }
            if (m_bubbleBox)
                m_bubbleBox->setVisible(false);
            return;
        }

        m_text->setVisible(true);
        if (m_typingDots)
            m_typingDots->setVisible(false);
        if (m_bubbleBox)
            m_bubbleBox->setVisible(true);

        // Only cap MAX width (3/4 of window). Don't enforce a minimum width.
        const int maxW = qMax(1, m_maxBubbleWidth);
        const QSize laidOutTextSize = m_text->layoutForMaxWidth(maxW);
        const int textWidgetW = qMax(1, laidOutTextSize.width());
        const int textH = qMax(1, laidOutTextSize.height());
        if (m_bubbleBox)
        {
            // Bubble size should include exactly one layer of inner padding from bubble layout.
            m_bubbleContentWidth = textWidgetW + kBubbleInnerPadX * 2;
            m_bubbleContentHeight = textH + kBubbleInnerPadY * 2;
            m_bubbleBox->setFixedSize(
                m_bubbleContentWidth,
                m_bubbleContentHeight);
            m_bubbleBox->updateGeometry();
        }

        if (chatLayoutDebugEnabled())
        {
            qInfo().noquote()
                << "[chat-layout][row]"
                << "text=" << m_text->toPlainText().left(80).replace(QLatin1Char('\n'), QLatin1Char(' '))
                << "maxW=" << maxW
                << "textW=" << textWidgetW
                << "textH=" << textH
                << "bubbleW=" << m_bubbleContentWidth
                << "bubbleH=" << m_bubbleContentHeight
                << "rowW=" << m_rowWidth
                << "widgetW=" << width();
        }

        m_text->updateGeometry();
        if (layout())
            layout()->activate();
        updateGeometry();
    }

    bool m_isUser{false};
    QLabel* m_avatar{nullptr};
    ThemeWidgets::ChatBubbleBox* m_bubbleBox{nullptr};
    ThemeWidgets::ChatBubbleTextView* m_text{nullptr};
    TypingDotsWidget* m_typingDots{nullptr};
    QWidget* m_selectSlot{nullptr};
    ThemeWidgets::ChatSelectionCheckBox* m_selectCheck{nullptr};
    int m_maxBubbleWidth{360};
    int m_rowWidth{520};
    int m_bubbleContentWidth{0};
    int m_bubbleContentHeight{0};
    int m_waitingContentHeight{0};
    bool m_waitingForResponse{false};
    int m_sourceMessageIndex{-1};
    bool m_checked{false};
};

} // namespace

class ChatWindow::Impl {
public:
    QString modelFolder;
    QString modelDir;

    QWidget* central{nullptr};
    ThemeWidgets::ChatListWidget* list{nullptr};
    QWidget* selectionActionBar{nullptr};
    QWidget* composerCard{nullptr};
    ThemeWidgets::ChatComposerEdit* input{nullptr};
    ThemeWidgets::IconButton* sendBtn{nullptr};
    ThemeWidgets::IconButton* mcpBtn{nullptr};
    ThemeWidgets::IconButton* clearBtn{nullptr};
    ThemeWidgets::IconButton* deleteSelectedBtn{nullptr};
    QLabel* deleteSelectedTextLabel{nullptr};
    QLabel* selectedCountLabel{nullptr};
    QLabel* countLabel{nullptr};
    McpServerPopup* mcpPopup{nullptr};
    QList<McpServerStatus> mcpStatuses;

    QPixmap userAvatar;
    QPixmap aiAvatar;

    ChatMessageWidget* currentAiBubble{nullptr};

    QJsonArray messages; // simplified format: {role, content}
    bool currentAiBubbleIsDraft{false};
    bool persistenceEnabled{true};
    bool messageSelectMode{false};
    QSet<int> selectedMessageIndices;

    bool relayoutQueued{false};
    bool busy{false};

    int streamingAssistantIndex{-1};

    bool hasSendableInput() const
    {
        return input && !input->toPlainText().trimmed().isEmpty();
    }

    Theme::IconToken composerButtonIconToken() const
    {
        return busy ? Theme::IconToken::ChatStop : Theme::IconToken::ChatSend;
    }

    QString composerButtonToolTip() const
    {
        return QCoreApplication::translate("ChatWindow", busy ? "中止" : "发送");
    }

    void updateSendButtonState()
    {
        if (!sendBtn)
            return;

        sendBtn->setTone(busy ? ThemeWidgets::IconButton::Tone::Danger
                              : ThemeWidgets::IconButton::Tone::Accent);
        sendBtn->setEnabled(busy || hasSendableInput());
        sendBtn->setToolTip(composerButtonToolTip());
        sendBtn->setIcon(Theme::themedIcon(composerButtonIconToken()));
    }

    int findAssistantSourceIndexFromDisk(const QString& content) const
    {
        if (modelFolder.isEmpty())
            return -1;

        const QJsonArray diskMessages = ConversationRepository::loadMessages(modelFolder);
        if (diskMessages.isEmpty())
            return -1;

        for (int i = diskMessages.size() - 1; i >= 0; --i)
        {
            const auto o = diskMessages.at(i).toObject();
            if (o.value(QStringLiteral("role")).toString() != QStringLiteral("assistant"))
                continue;

            const QString diskContent = o.value(QStringLiteral("content")).toString();
            if (!content.isEmpty() && diskContent == content)
                return i;
            if (content.isEmpty() && diskContent.trimmed().isEmpty())
                return i;
        }

        if (!content.isEmpty())
        {
            for (int i = diskMessages.size() - 1; i >= 0; --i)
            {
                const auto o = diskMessages.at(i).toObject();
                if (o.value(QStringLiteral("role")).toString() != QStringLiteral("assistant"))
                    continue;
                const QString diskContent = o.value(QStringLiteral("content")).toString();
                if (diskContent.startsWith(content) || content.startsWith(diskContent))
                    return i;
            }
        }

        return -1;
    }

    void bindCurrentAiBubbleSourceIndexIfNeeded()
    {
        if (!currentAiBubble)
            return;
        if (currentAiBubble->sourceMessageIndex() >= 0)
            return;

        const int idx = findAssistantSourceIndexFromDisk(currentAiBubble->content());
        if (idx < 0)
            return;

        currentAiBubble->setSourceMessageIndex(idx);
        currentAiBubble->setSelectionModeEnabled(messageSelectMode);
        currentAiBubble->setMessageChecked(selectedMessageIndices.contains(idx));
    }

    void scheduleRelayout(QObject* context)
    {
        if (relayoutQueued) return;
        relayoutQueued = true;
        QTimer::singleShot(16, context, [this]{
            relayoutQueued = false;
            applyBubbleWidthForAll();
        });
    }

    int viewportRowWidth() const
    {
        const int vw = (list && list->viewport()) ? list->viewport()->width() : 520;
        return qMax(1, vw - 20);
    }

    int computeBubbleMaxWidth() const
    {
        const int vw = viewportRowWidth();
        const int safe = qMax(0, vw - 32);
        const int max = int(safe * 0.75);
        return qMax(1, max);
    }

    void applyBubbleWidthForAll()
    {
        if (!list) return;
        const int rowW = viewportRowWidth();
        const int maxW = computeBubbleMaxWidth();

        if (chatLayoutDebugEnabled())
        {
            qInfo().noquote()
                << "[chat-layout][relayout]"
                << "viewportW=" << ((list && list->viewport()) ? list->viewport()->width() : -1)
                << "rowW=" << rowW
                << "maxBubbleW=" << maxW
                << "items=" << list->count();
        }

        for (int i = 0; i < list->count(); ++i)
        {
            auto* item = list->item(i);
            if (!item) continue;
            if (auto* w = qobject_cast<ChatMessageWidget*>(list->itemWidget(item)))
            {
                w->setMaxBubbleWidth(maxW);
                w->setRowWidth(0);
                const QSize hint = w->sizeHint();
                item->setSizeHint(QSize(0, hint.height()));
            }
        }
        forceListRelayout();
    }

    void forceListRelayout()
    {
        if (!list) return;
        // Force immediate geometry recalculation so bubble sizing changes are visible right away.
        list->doItemsLayout();
        list->updateGeometry();
        list->viewport()->update();
    }

    void updateInputMetrics()
    {
        if (!input) return;

        const int targetHeight = input->preferredHeight(kComposerMinLines, kComposerMaxLines);
        if (input->height() != targetHeight)
            input->setFixedHeight(targetHeight);

        const QFontMetrics fm(input->font());
        const int maxDocumentHeight = fm.lineSpacing() * kComposerMaxLines;
        const bool overflow = input->documentHeight() > maxDocumentHeight;
        input->setVerticalScrollBarPolicy(overflow ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    }

    void updateInputCount()
    {
        if (!countLabel || !input) return;
        countLabel->setText(QString::number(input->toPlainText().size()));
        updateSendButtonState();
    }

    void updateMcpPopup()
    {
        if (mcpPopup)
            mcpPopup->setStatuses(mcpStatuses);
    }

    static bool isVisibleMessageObject(const QJsonObject& o)
    {
        const QString role = o.value(QStringLiteral("role")).toString();
        const QString content = o.value(QStringLiteral("content")).toString();
        if (role == QStringLiteral("tool"))
            return false;
        if (role == QStringLiteral("assistant") && content.trimmed().isEmpty())
            return false;
        return role == QStringLiteral("user") || role == QStringLiteral("assistant");
    }

    void updateSelectionUi()
    {
        if (selectionActionBar)
            selectionActionBar->setVisible(messageSelectMode);

        if (deleteSelectedBtn)
        {
            deleteSelectedBtn->setVisible(messageSelectMode);
            deleteSelectedBtn->setEnabled(messageSelectMode && !selectedMessageIndices.isEmpty());
        }

        if (deleteSelectedTextLabel)
            deleteSelectedTextLabel->setVisible(messageSelectMode);

        if (selectedCountLabel)
        {
            selectedCountLabel->setVisible(messageSelectMode);
            selectedCountLabel->setText(QCoreApplication::translate("ChatWindow", "已勾选 %1 条").arg(selectedMessageIndices.size()));
        }

        if (!list)
            return;

        for (int i = 0; i < list->count(); ++i)
        {
            auto* item = list->item(i);
            if (!item)
                continue;
            auto* bubble = qobject_cast<ChatMessageWidget*>(list->itemWidget(item));
            if (!bubble)
                continue;
            bubble->setSelectionModeEnabled(messageSelectMode);
            bubble->setMessageChecked(selectedMessageIndices.contains(bubble->sourceMessageIndex()));
        }
    }

    void setMessageSelection(int sourceMessageIndex, bool checked)
    {
        if (sourceMessageIndex < 0)
            return;

        if (checked)
        {
            messageSelectMode = true;
            selectedMessageIndices.insert(sourceMessageIndex);
        }
        else
        {
            selectedMessageIndices.remove(sourceMessageIndex);
            if (selectedMessageIndices.isEmpty())
                messageSelectMode = false;
        }

        updateSelectionUi();
    }

    bool deleteSelectedMessages(QWidget* parent, const QString& modelFolder)
    {
        if (selectedMessageIndices.isEmpty())
            return false;

        const auto ret = QMessageBox::question(parent,
                                               QCoreApplication::translate("ChatWindow", "确认"),
                                               QCoreApplication::translate("ChatWindow", "确定要删除已勾选的消息吗？"));
        if (ret != QMessageBox::Yes)
            return false;

        QList<int> removeIndices = selectedMessageIndices.values();
        std::sort(removeIndices.begin(), removeIndices.end(), std::greater<int>());
        for (const int idx : removeIndices)
        {
            if (idx < 0 || idx >= messages.size())
                continue;
            messages.removeAt(idx);
        }

        selectedMessageIndices.clear();
        messageSelectMode = false;
        currentAiBubble = nullptr;
        currentAiBubbleIsDraft = false;
        streamingAssistantIndex = -1;

        if (!modelFolder.isEmpty())
            ConversationRepository::saveMessages(modelFolder, messages);

        rebuildFromMessages();
        return true;
    }

    void rebuildFromMessages()
    {
        list->clear();
        currentAiBubble = nullptr;

        const int maxW = computeBubbleMaxWidth();
        const int rowW = viewportRowWidth();

        for (int srcIndex = 0; srcIndex < messages.size(); ++srcIndex)
        {
            const auto o = messages.at(srcIndex).toObject();
            const QString role = o.value("role").toString();
            const QString content = o.value("content").toString();

            if (!isVisibleMessageObject(o))
                continue;

            const bool isUser = (role == "user");

            auto* item = new QListWidgetItem(list);
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);

            auto* bubble = new ChatMessageWidget(isUser ? userAvatar : aiAvatar, content, isUser);
            bubble->setMaxBubbleWidth(maxW);
            bubble->setRowWidth(0);
            bubble->setSourceMessageIndex(srcIndex);
            bubble->setSelectionModeEnabled(messageSelectMode);
            bubble->setMessageChecked(selectedMessageIndices.contains(srcIndex));
            QObject::connect(bubble, &ChatMessageWidget::messageSelectionToggled, list, [this](int index, bool checked) {
                setMessageSelection(index, checked);
            });

            const QSize hint = bubble->sizeHint();
            item->setSizeHint(QSize(0, hint.height()));
            list->addItem(item);
            list->setItemWidget(item, bubble);
        }

        list->scrollToBottom();
        forceListRelayout();
        updateSelectionUi();
    }

    void pushMessage(const QString& role, const QString& content)
    {
        QJsonObject o;
        o["role"] = role;
        o["content"] = content;
        messages.append(o);
        if (persistenceEnabled)
            ConversationRepository::saveMessages(modelFolder, messages);
    }

    void beginStreamingAssistant()
    {
        if (!persistenceEnabled)
        {
            streamingAssistantIndex = -1;
            return;
        }

        // Ensure persisted history has exactly one assistant placeholder for this reply.
        streamingAssistantIndex = -1;
        if (modelFolder.isEmpty()) return;

        // If last message is already an assistant placeholder (empty), reuse it.
        if (!messages.isEmpty())
        {
            const auto last = messages.last().toObject();
            if (last.value("role").toString() == QStringLiteral("assistant")
                && last.value("content").toString().isEmpty())
            {
                streamingAssistantIndex = messages.size() - 1;
                return;
            }
        }

        QJsonObject o;
        o["role"] = QStringLiteral("assistant");
        o["content"] = QString();
        messages.append(o);
        streamingAssistantIndex = messages.size() - 1;
        ConversationRepository::saveMessages(modelFolder, messages);
    }

    void updateStreamingAssistantContent(const QString& content)
    {
        if (!persistenceEnabled)
            return;

        if (streamingAssistantIndex < 0 || streamingAssistantIndex >= messages.size()) return;
        auto o = messages.at(streamingAssistantIndex).toObject();
        if (o.value("role").toString() != QStringLiteral("assistant")) return;
        o["content"] = content;
        messages[streamingAssistantIndex] = o;
        ConversationRepository::saveMessages(modelFolder, messages);
    }

    void finalizeStreamingAssistant(const QString& finalText)
    {
        if (streamingAssistantIndex >= 0)
        {
            updateStreamingAssistantContent(finalText);
        }
        streamingAssistantIndex = -1;
    }

    // NOTE: streaming should NOT touch persisted chat history.
    // We keep the assistant bubble purely in UI during streaming and persist only once at finish.
};

ChatWindow::ChatWindow(QWidget* parent)
    : QMainWindow(parent), d(new Impl)
{
    setWindowTitle(tr("聊天"));
#if defined(Q_OS_LINUX)
    setWindowFlag(Qt::Window, true);
    setWindowFlag(Qt::CustomizeWindowHint, false);
    setWindowFlag(Qt::WindowMaximizeButtonHint, false);
#endif
    resize(520, 640);

    d->central = new QWidget(this);
    d->central->setObjectName(QStringLiteral("chatCentral"));
    setCentralWidget(d->central);

    auto* root = new QVBoxLayout(d->central);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    d->list = new ThemeWidgets::ChatListWidget(d->central);
    d->list->setSpacing(6);
    d->list->setUniformItemSizes(false);
    d->list->setSelectionMode(QAbstractItemView::NoSelection);
    d->list->setFocusPolicy(Qt::NoFocus);
    d->list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    d->list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    d->list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    root->addWidget(d->list, 1);

    d->selectionActionBar = new QWidget(d->central);
    d->selectionActionBar->setObjectName(QStringLiteral("chatSelectionActionBar"));
    auto* selectionBarLayout = new QHBoxLayout(d->selectionActionBar);
    selectionBarLayout->setContentsMargins(2, 0, 2, 0);
    selectionBarLayout->setSpacing(8);
    d->selectionActionBar->setVisible(false);
    root->addWidget(d->selectionActionBar, 0);

    d->composerCard = new QWidget(d->central);
    d->composerCard->setObjectName(QStringLiteral("chatComposerCard"));
    auto* composerLayout = new QVBoxLayout(d->composerCard);
    composerLayout->setContentsMargins(14, 14, 14, 14);
    composerLayout->setSpacing(6);

    auto* editorRow = new QHBoxLayout();
    editorRow->setContentsMargins(0, 0, 0, 0);
    editorRow->setSpacing(10);

    d->input = new ThemeWidgets::ChatComposerEdit(d->composerCard);
    d->input->document()->setDocumentMargin(kComposerInputDocumentMargin);
    d->input->setPlaceholderText(tr("输入消息... (Enter 发送 / Shift+Enter 换行)"));
    d->sendBtn = new ThemeWidgets::IconButton(d->composerCard);
    d->sendBtn->setTone(ThemeWidgets::IconButton::Tone::Accent);
    d->sendBtn->setIconLogicalSize(kComposerSendIconSize);
    d->sendBtn->setFixedSize(kComposerSendButtonSize, kComposerSendButtonSize);
    d->updateSendButtonState();

    editorRow->addWidget(d->input, 1);
    editorRow->addWidget(d->sendBtn, 0, Qt::AlignRight | Qt::AlignBottom);
    composerLayout->addLayout(editorRow);

    auto* footerRow = new QHBoxLayout();
    footerRow->setContentsMargins(0, 0, 0, 0);
    footerRow->setSpacing(8);

    d->clearBtn = new ThemeWidgets::IconButton(d->composerCard);
    d->clearBtn->setTone(ThemeWidgets::IconButton::Tone::Ghost);
    d->clearBtn->setIconLogicalSize(kComposerClearIconSize);
    d->clearBtn->setToolTip(tr("清除"));
    d->clearBtn->setFixedSize(kComposerFooterControlSize, kComposerFooterControlSize);
    d->clearBtn->setIcon(Theme::themedIcon(Theme::IconToken::ChatClear));

    d->mcpBtn = new ThemeWidgets::IconButton(d->composerCard);
    d->mcpBtn->setTone(ThemeWidgets::IconButton::Tone::Ghost);
    d->mcpBtn->setIconLogicalSize(kComposerClearIconSize);
    d->mcpBtn->setToolTip(tr("MCP"));
    d->mcpBtn->setFixedSize(kComposerFooterControlSize, kComposerFooterControlSize);
    d->mcpBtn->setIcon(Theme::themedIcon(Theme::IconToken::ChatMcp));

    d->mcpPopup = new McpServerPopup(this);
    d->mcpPopup->setStatuses(d->mcpStatuses);

    d->deleteSelectedBtn = new ThemeWidgets::IconButton(d->selectionActionBar);
    d->deleteSelectedBtn->setTone(ThemeWidgets::IconButton::Tone::Ghost);
    d->deleteSelectedBtn->setIconLogicalSize(kComposerClearIconSize);
    d->deleteSelectedBtn->setToolTip(tr("删除"));
    d->deleteSelectedBtn->setFixedSize(kComposerFooterControlSize, kComposerFooterControlSize);
    d->deleteSelectedBtn->setIcon(Theme::themedIcon(Theme::IconToken::ChatDelete));
    d->deleteSelectedBtn->setVisible(false);

    d->deleteSelectedTextLabel = new QLabel(tr("删除"), d->selectionActionBar);
    d->deleteSelectedTextLabel->setObjectName(QStringLiteral("chatDeleteSelectedTextLabel"));
    d->deleteSelectedTextLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    d->deleteSelectedTextLabel->setVisible(false);

    d->selectedCountLabel = new QLabel(tr("已勾选 0 条"), d->selectionActionBar);
    d->selectedCountLabel->setObjectName(QStringLiteral("chatSelectedCountLabel"));
    d->selectedCountLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    d->selectedCountLabel->setVisible(false);

    d->countLabel = new QLabel(QStringLiteral("0"), d->composerCard);
    d->countLabel->setObjectName(QStringLiteral("chatComposerCountLabel"));
    d->countLabel->setAlignment(Qt::AlignCenter);
    d->countLabel->setFixedSize(kComposerSendButtonSize, kComposerFooterControlSize);
    d->countLabel->setContentsMargins(0, 5, 0, 0);

    footerRow->addWidget(d->mcpBtn, 0, Qt::AlignLeft | Qt::AlignVCenter);
    footerRow->addWidget(d->clearBtn, 0, Qt::AlignLeft | Qt::AlignVCenter);
    selectionBarLayout->addWidget(d->deleteSelectedBtn, 0, Qt::AlignLeft | Qt::AlignVCenter);
    selectionBarLayout->addWidget(d->deleteSelectedTextLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);
    selectionBarLayout->addWidget(d->selectedCountLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);
    selectionBarLayout->addStretch(1);

    footerRow->addStretch(1);
    footerRow->addWidget(d->countLabel, 0, Qt::AlignRight | Qt::AlignVCenter);
    composerLayout->addLayout(footerRow);

    root->addWidget(d->composerCard);

    // 默认头像：统一使用 avator-icon.png（用户头像 + 模型无 png 时的 AI 默认头像）
    const QString defaultAvatarPath = appResourcePath(QStringLiteral("icons/avator-icon.png"));
    d->userAvatar = QPixmap(defaultAvatarPath);
    if (d->userAvatar.isNull())
    {
        // 兜底：如果资源缺失，至少用 app-icon
        d->userAvatar = QPixmap(appResourcePath(QStringLiteral("icons/app-icon.png")));
    }

    auto sendNow = [this]{
        if (d->modelFolder.isEmpty()) return;
        if (d->busy) return;

        const QString text = d->input->toPlainText().trimmed();
        if (text.isEmpty()) return;

        // De-dup: some platforms/widgets can emit both "clicked" and our custom sendRequested
        // for a single user action. Guard by time+content.
        static QString s_lastText;
        static qint64 s_lastMs = 0;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (text == s_lastText && (nowMs - s_lastMs) < 400)
            return;
        s_lastText = text;
        s_lastMs = nowMs;

        d->input->clear();

        emit requestSendMessage(d->modelFolder, text);
    };

    auto abortNow = [this] {
        if (!d->busy)
            return;

        emit requestAbortCurrentTask();
    };

    connect(d->sendBtn, &QToolButton::clicked, this, [sendNow, abortNow, this] {
        if (!d)
            return;
        if (d->busy)
        {
            abortNow();
            return;
        }
        sendNow();
    });
    connect(d->input, &ThemeWidgets::ChatComposerEdit::sendRequested, this, sendNow);
    connect(d->input, &ThemeWidgets::ChatComposerEdit::metricsChanged, this, [this] {
        if (!d) return;
        d->updateInputMetrics();
    });
    connect(d->input, &QTextEdit::textChanged, this, [this] {
        if (!d) return;
        d->updateInputCount();
    });

    connect(d->clearBtn, &QToolButton::clicked, this, [this]{
        if (d->modelFolder.isEmpty()) return;
        const auto ret = QMessageBox::question(this, tr("确认"), tr("确定要清除当前模型的聊天记录吗？"));
        if (ret != QMessageBox::Yes) return;
        emit requestClearChat(d->modelFolder);
    });

    connect(d->mcpBtn, &QToolButton::clicked, this, [this] {
        if (!d || !d->mcpPopup || !d->mcpBtn)
            return;

        if (d->mcpPopup->isVisible())
        {
            d->mcpPopup->hide();
            return;
        }

        d->updateMcpPopup();
        d->mcpPopup->popupNearAnchor(d->mcpBtn);
        if (d->mcpStatuses.isEmpty())
            emit requestRefreshMcpServerStatuses();
    });

    connect(d->mcpPopup, &McpServerPopup::enabledChanged, this, [this](const QString& serverName, bool enabled) {
        emit requestSetMcpServerEnabled(serverName, enabled);
    });

    connect(d->deleteSelectedBtn, &QToolButton::clicked, this, [this] {
        if (!d)
            return;
        if (d->deleteSelectedMessages(this, d->modelFolder))
            d->scheduleRelayout(this);
    });

    // Responsive relayout on resize
    // Also update widths when the viewport itself changes size (more immediate than rangeChanged)
    d->list->viewport()->installEventFilter(this);

    // Initial relayout after show; without this some bubbles get an incorrect first width.
    d->updateInputMetrics();
    d->updateInputCount();
    d->updateSelectionUi();
    d->scheduleRelayout(this);
}

ChatWindow::~ChatWindow() = default;

QString ChatWindow::currentAssistantDraft() const
{
    if (!d || !d->currentAiBubble) return {};
    return d->currentAiBubble->content();
}

void ChatWindow::setCurrentModel(const QString& modelFolder, const QString& modelDir)
{
    d->modelFolder = modelFolder;
    d->modelDir = modelDir;

    // Always reset AI avatar when model changes, so switching to "no png" doesn't keep old one.
    d->aiAvatar = {};

    // AI 头像：取模型目录第一张 png，否则回落到默认头像（avator-icon.png）
    const QString aiPng = firstPngInDir(modelDir);
    if (!aiPng.isEmpty())
    {
        d->aiAvatar = QPixmap(aiPng);
    }

    if (d->aiAvatar.isNull())
    {
        d->aiAvatar = d->userAvatar;
    }

    d->messageSelectMode = false;
    d->selectedMessageIndices.clear();
    d->updateSelectionUi();
    d->updateMcpPopup();

    loadFromDisk(modelFolder);

    // After rebuilding, ensure widths match the current viewport size.
    d->scheduleRelayout(this);
}

void ChatWindow::setBusy(bool busy)
{
    if (!d)
        return;

    d->busy = busy;
    d->updateSendButtonState();
}

void ChatWindow::sealCurrentAiBubble()
{
    if (!d || !d->currentAiBubble)
        return;

    d->currentAiBubble->setWaitingForResponse(false);
    d->bindCurrentAiBubbleSourceIndexIfNeeded();
    d->currentAiBubbleIsDraft = false;
    d->currentAiBubble = nullptr;
    d->scheduleRelayout(this);
}

void ChatWindow::setPersistenceEnabled(bool enabled)
{
    if (!d)
        return;
    d->persistenceEnabled = enabled;
}

void ChatWindow::setMcpServerStatuses(const QList<McpServerStatus>& statuses)
{
    if (!d)
        return;

    d->mcpStatuses = statuses;
    d->updateMcpPopup();
}

void ChatWindow::cancelCurrentAiDraftBubble()
{
    if (!d || !d->currentAiBubble || !d->list)
        return;

    // Remove only the in-flight UI draft bubble; persisted history remains runtime-owned.
    for (int i = d->list->count() - 1; i >= 0; --i)
    {
        QListWidgetItem* item = d->list->item(i);
        if (!item)
            continue;

        QWidget* widget = d->list->itemWidget(item);
        if (widget != d->currentAiBubble)
            continue;

        QWidget* taken = d->list->itemWidget(item);
        d->list->removeItemWidget(item);
        delete taken;
        delete d->list->takeItem(i);
        break;
    }

    d->currentAiBubble = nullptr;
    d->currentAiBubbleIsDraft = false;
    d->scheduleRelayout(this);
}

void ChatWindow::appendUserMessage(const QString& text)
{
    if (d->modelFolder.isEmpty()) return;

    if (!d->persistenceEnabled)
    {
        d->messages = ConversationRepository::loadMessages(d->modelFolder);
        d->rebuildFromMessages();
        d->scheduleRelayout(this);
        return;
    }

    // Runtime migration path may persist the same user message before UI sync.
    // Keep this method idempotent to avoid duplicated user rows.
    if (!d->messages.isEmpty())
    {
        const auto last = d->messages.last().toObject();
        if (last.value("role").toString() == QStringLiteral("user")
            && last.value("content").toString() == text)
        {
            d->rebuildFromMessages();
            d->scheduleRelayout(this);
            return;
        }
    }

    d->pushMessage("user", text);
    d->rebuildFromMessages();
    d->scheduleRelayout(this);
}

void ChatWindow::appendAiMessageStart()
{
    // Start a streaming assistant bubble.
    if (d->modelFolder.isEmpty()) return;

    if (d->currentAiBubble)
        return;

    // Persist a placeholder assistant message ONCE so streaming doesn't create multiple assistant rows.
    if (d->persistenceEnabled)
        d->beginStreamingAssistant();

    // UI-only draft bubble; the persisted content is updated via setAiMessageContent()/appendAiToken().
    d->currentAiBubbleIsDraft = true;

    if (d->list)
    {
        auto* item = new QListWidgetItem(d->list);
        auto* w = new ChatMessageWidget(d->aiAvatar, QString(), /*isUser*/false, nullptr);
        w->setWaitingForResponse(true);
        w->setMaxBubbleWidth(d->computeBubbleMaxWidth());
        w->setRowWidth(0);
        int sourceIndex = d->streamingAssistantIndex;
        if (sourceIndex < 0)
            sourceIndex = d->findAssistantSourceIndexFromDisk(QString());
        w->setSourceMessageIndex(sourceIndex);
        w->setSelectionModeEnabled(d->messageSelectMode);
        w->setMessageChecked(d->selectedMessageIndices.contains(sourceIndex));
        QObject::connect(w, &ChatMessageWidget::messageSelectionToggled, d->list, [this](int index, bool checked) {
            d->setMessageSelection(index, checked);
        });
        const QSize hint = w->sizeHint();
        item->setSizeHint(QSize(0, hint.height()));
        d->list->addItem(item);
        d->list->setItemWidget(item, w);
        d->currentAiBubble = w;
        d->list->scrollToBottom();
        d->scheduleRelayout(this);
    }
}

void ChatWindow::appendAiToken(const QString& token)
{
    if (!d) return;
    if (!d->currentAiBubble)
        appendAiMessageStart();
    if (!d->currentAiBubble)
        return;

    d->currentAiBubble->appendToken(token);

    // Streaming: update the single persisted assistant placeholder, don't append new rows.
    if (d->persistenceEnabled && !d->modelFolder.isEmpty())
        d->updateStreamingAssistantContent(d->currentAiBubble->content());
    else
        d->bindCurrentAiBubbleSourceIndexIfNeeded();

    // resize list item
    if (d->list)
    {
        auto* item = d->list->item(d->list->count() - 1);
        if (item)
        {
            const QSize hint = d->currentAiBubble->sizeHint();
            item->setSizeHint(QSize(0, hint.height()));
        }
        d->list->scrollToBottom();
    }
}

void ChatWindow::appendAiMessageFinish()
{
    if (d && d->currentAiBubble && !d->modelFolder.isEmpty())
    {
        const QString finalText = d->currentAiBubble->content();

        // For streaming we already created a placeholder and updated it; just finalize it here.
        if (d->currentAiBubbleIsDraft)
        {
            d->finalizeStreamingAssistant(finalText);
        }
        else
        {
            // Non-stream path: keep old behavior (append once, guard duplicates).
            if (!d->messages.isEmpty())
            {
                const auto last = d->messages.last().toObject();
                if (!(last.value("role").toString() == QStringLiteral("assistant")
                      && last.value("content").toString() == finalText))
                {
                    d->pushMessage(QStringLiteral("assistant"), finalText);
                }
            }
            else
            {
                d->pushMessage(QStringLiteral("assistant"), finalText);
            }
        }

        d->rebuildFromMessages();
        d->applyBubbleWidthForAll();
        if (d->list) d->list->scrollToBottom();
    }

    d->currentAiBubble = nullptr;
    d->currentAiBubbleIsDraft = false;
}

void ChatWindow::setAiMessageContent(const QString& content)
{
    // Set/replace the assistant draft bubble content.
    if (d->modelFolder.isEmpty()) return;

    if (!d->currentAiBubble)
    {
        appendAiMessageStart();
    }

    if (d->currentAiBubble)
    {
        d->currentAiBubble->setContent(content);
        d->currentAiBubbleIsDraft = true;

        // Keep persisted placeholder in sync.
        if (d->persistenceEnabled)
            d->updateStreamingAssistantContent(content);
        else
            d->bindCurrentAiBubbleSourceIndexIfNeeded();

        if (d->list)
        {
            auto* item = d->list->item(d->list->count() - 1);
            if (item)
            {
                const QSize hint = d->currentAiBubble->sizeHint();
                item->setSizeHint(QSize(0, hint.height()));
            }
            d->list->scrollToBottom();
        }
    }
}

void ChatWindow::loadFromDisk(const QString& modelFolder)
{
    if (modelFolder.isEmpty()) return;
    d->messages = ConversationRepository::loadMessages(modelFolder);
    d->currentAiBubble = nullptr;
    d->currentAiBubbleIsDraft = false;
    d->streamingAssistantIndex = -1;
    d->messageSelectMode = false;
    d->selectedMessageIndices.clear();
    d->rebuildFromMessages();
}

bool ChatWindow::event(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange)
    {
        setWindowTitle(tr("聊天"));
        if (d)
        {
            if (d->input) d->input->setPlaceholderText(tr("输入消息... (Enter 发送 / Shift+Enter 换行)"));
            d->updateSendButtonState();
            if (d->mcpBtn) d->mcpBtn->setToolTip(tr("MCP"));
            if (d->clearBtn) d->clearBtn->setToolTip(tr("清除"));
            if (d->deleteSelectedBtn) d->deleteSelectedBtn->setToolTip(tr("删除"));
            if (d->deleteSelectedTextLabel) d->deleteSelectedTextLabel->setText(tr("删除"));
            d->updateMcpPopup();
            d->updateSelectionUi();
        }
    }

    if (e->type() == QEvent::Resize)
    {
        if (d && d->list)
        {
            d->scheduleRelayout(this);
        }
        if (d)
        {
            d->updateInputMetrics();
            if (d->mcpPopup && d->mcpPopup->isVisible() && d->mcpBtn)
                d->mcpPopup->popupNearAnchor(d->mcpBtn);
        }
    }

    if (e->type() == QEvent::ApplicationPaletteChange
        || e->type() == QEvent::PaletteChange
        || e->type() == QEvent::ThemeChange
        || e->type() == QEvent::StyleChange)
    {
        if (d)
        {
            d->updateSendButtonState();
            if (d->mcpBtn) d->mcpBtn->setIcon(Theme::themedIcon(Theme::IconToken::ChatMcp));
            if (d->clearBtn) d->clearBtn->setIcon(Theme::themedIcon(Theme::IconToken::ChatClear));
            if (d->deleteSelectedBtn) d->deleteSelectedBtn->setIcon(Theme::themedIcon(Theme::IconToken::ChatDelete));
            d->updateMcpPopup();
            if (d->list) d->list->viewport()->update();
            d->updateInputMetrics();
            d->scheduleRelayout(this);
        }
    }

    return QMainWindow::event(e);
}

bool ChatWindow::eventFilter(QObject* obj, QEvent* e)
{
    if (d && d->list && obj == d->list->viewport())
    {
        if (e->type() == QEvent::Resize)
        {
            // Debounced relayout avoids jitter during fast narrow/wide drag resize.
            d->scheduleRelayout(this);
            return false;
        }
    }

    return QMainWindow::eventFilter(obj, e);
}

#include "ChatWindow.moc"

void ChatWindow::finalizeAssistantMessage(const QString& content)
{
    finalizeAssistantMessage(content, /*ensureBubbleExists*/true);
}

void ChatWindow::finalizeAssistantMessage(const QString& content, bool ensureBubbleExists)
{
    if (!d || d->modelFolder.isEmpty())
        return;

    if (!d->persistenceEnabled)
    {
        d->currentAiBubble = nullptr;
        d->currentAiBubbleIsDraft = false;
        d->messages = ConversationRepository::loadMessages(d->modelFolder);
        d->rebuildFromMessages();
        d->scheduleRelayout(this);
        return;
    }

    // Make sure we have a visible assistant bubble representing THIS reply.
    if (ensureBubbleExists)
    {
        if (!d->currentAiBubble)
            appendAiMessageStart();
        if (d->currentAiBubble)
            d->currentAiBubble->setContent(content);
        d->bindCurrentAiBubbleSourceIndexIfNeeded();
    }

    // Persist final assistant content exactly once:
    // - If we are streaming, there is already a placeholder assistant row (empty at start) which we update.
    // - If we are non-streaming, update last assistant if its content is empty, otherwise append.

    // 1) Streaming path: placeholder index is tracked.
    if (d->streamingAssistantIndex >= 0 && d->streamingAssistantIndex < d->messages.size())
    {
        d->updateStreamingAssistantContent(content);
        d->streamingAssistantIndex = -1;
    }
    else
    {
        // 2) Non-stream path: prefer to update last assistant if it exists and is empty.
        if (!d->messages.isEmpty())
        {
            const auto last = d->messages.last().toObject();
            const QString role = last.value("role").toString();
            const QString lastContent = last.value("content").toString();

            if (role == QStringLiteral("assistant") && lastContent.isEmpty())
            {
                // Replace the placeholder.
                QJsonObject o = last;
                o["content"] = content;
                d->messages[d->messages.size() - 1] = o;
                ConversationRepository::saveMessages(d->modelFolder, d->messages);
            }
            else if (!(role == QStringLiteral("assistant") && lastContent == content))
            {
                d->pushMessage(QStringLiteral("assistant"), content);
            }
        }
        else
        {
            d->pushMessage(QStringLiteral("assistant"), content);
        }
    }

    // Rebuild UI from persisted messages to avoid leaving a draft-only bubble around.
    d->currentAiBubble = nullptr;
    d->currentAiBubbleIsDraft = false;

    d->rebuildFromMessages();
    d->scheduleRelayout(this);
}
