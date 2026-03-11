#include "ui/ChatWindow.hpp"

#include "common/SettingsManager.hpp"
#include "common/Utils.hpp"

#include <QAbstractTextDocumentLayout>
#include <QBoxLayout>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include "ui/era-style/EraButton.hpp"
#include "ui/era-style/EraTextEdit.hpp"
#include "ui/era-style/EraStyleColor.hpp"
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextEdit>
#include <QKeyEvent>
#include <QTextOption>
#include <QTimer>
#include <QPlainTextEdit>

namespace {

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

QJsonArray loadChatMessages(const QString& modelFolder)
{
    QFile f(SettingsManager::instance().chatPathForModel(modelFolder));
    if (!f.exists()) return {};
    if (!f.open(QIODevice::ReadOnly)) return {};
    QJsonParseError e;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &e);
    if (e.error != QJsonParseError::NoError) return {};
    return doc.object().value("messages").toArray();
}

void saveChatMessages(const QString& modelFolder, const QJsonArray& messages)
{
    QDir dir(SettingsManager::instance().chatsDir());
    if (!dir.exists()) { dir.mkpath("."); }

    QJsonObject o;
    o["messages"] = messages;

    QFile f(SettingsManager::instance().chatPathForModel(modelFolder));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    }
}

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

        m_bubbleBox = new QWidget(this);
        auto* bubbleLay = new QVBoxLayout(m_bubbleBox);
        bubbleLay->setContentsMargins(12, 8, 12, 8);
        bubbleLay->setSpacing(0);

        m_text = new QTextBrowser(m_bubbleBox);
        m_text->setText(text);
        m_text->setOpenExternalLinks(true);
        m_text->setFrameShape(QFrame::NoFrame);
        m_text->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_text->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        m_text->setContextMenuPolicy(Qt::DefaultContextMenu);
        m_text->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
        m_text->document()->setDocumentMargin(0.0);
        m_text->setContentsMargins(0, 0, 0, 0);
        updateBubbleTextStyle();
        m_text->setWordWrapMode(QTextOption::WordWrap);
        m_text->setLineWrapMode(QTextEdit::WidgetWidth);
        m_text->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        bubbleLay->addWidget(m_text);
        m_bubbleBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

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
            lay->addStretch(1);
        }

        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

        // Initial sizing
        syncTextSizeToContent();
    }

    void setAvatar(const QPixmap& avatar)
    {
        const qreal dpr = devicePixelRatioF();
        m_avatar->setPixmap(circleAvatar(avatar, 32, dpr));
    }

    void updateBubbleTextStyle()
    {
        const QColor text = m_isUser ? EraStyleColor::MainText : EraStyleColor::BasicWhite;
        m_text->setStyleSheet(QStringLiteral(
            "QTextBrowser{"
            "background:transparent; padding:0px; margin:0px; border:none; color:%1; }"
            "QTextBrowser > QWidget { margin:0px; padding:0px; background:transparent; }"
        ).arg(text.name(QColor::HexRgb)));
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
        m_text->moveCursor(QTextCursor::End);
        m_text->insertPlainText(t);
        m_text->moveCursor(QTextCursor::End);
        updateBubbleTextStyle();
        syncTextSizeToContent();
        updateGeometry();
        update();
    }

    void setContent(const QString& c)
    {
        m_text->setText(c);
        updateBubbleTextStyle();
        syncTextSizeToContent();
        updateGeometry();
        update();
    }

    QString content() const
    {
        return m_text ? m_text->toPlainText() : QString();
    }

    QSize sizeHint() const override
    {
        // Height = max(avatar, text bubble) + paddings.
        const int rowTopBottom = 16;
        const int bubbleH = m_bubbleBox ? m_bubbleBox->height() : 0;
        const int h = qMax(32, bubbleH) + rowTopBottom;
        return { m_rowWidth, h };
    }

protected:
    void paintEvent(QPaintEvent* ev) override
    {
        QWidget::paintEvent(ev);

        QRect bubbleRect = m_bubbleBox ? m_bubbleBox->geometry() : QRect();

        // Clamp bubble to our content area so it never overlaps layout margins.
        const QMargins cm = contentsMargins();
        const QRect contentRect = rect().adjusted(cm.left(), cm.top(), -cm.right(), -cm.bottom());
        bubbleRect = bubbleRect.intersected(contentRect);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        QColor bg;
        QColor border;
        if (m_isUser)
        {
            bg = EraStyleColor::BasicWhite;
            border = EraStyleColor::PrimaryBorder;
        }
        else
        {
            bg = EraStyleColor::Link;
            border = EraStyleColor::LinkClick;
        }

        p.setPen(QPen(border, 1.0));
        p.setBrush(bg);
        p.drawRoundedRect(bubbleRect, 14, 14);
    }

private:
    void syncTextSizeToContent()
    {
        if (!m_text) return;

        m_text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_text->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        QTextDocument* doc = m_text->document();
        doc->setDocumentMargin(0.0);

        const QFontMetrics fm(m_text->font());

        // Only cap MAX width (3/4 of window). Don't enforce a minimum width.
        const int maxW = qMax(1, m_maxBubbleWidth);

        // Phase 1: measure ideal width without wrapping.
        {
            QTextOption opt;
            opt.setWrapMode(QTextOption::NoWrap);
            doc->setDefaultTextOption(opt);
        }
        doc->setTextWidth(-1);
        doc->adjustSize();

        const int idealW = qMax(1, int(std::ceil(doc->idealWidth())));

        const bool empty = m_text->toPlainText().isEmpty();
        int w = empty ? fm.horizontalAdvance(QStringLiteral("…")) : idealW;
        w = qMin(w, maxW);

        // Phase 2: if clamped, enable wrapping and compute final height.
        if (w >= maxW)
        {
            QTextOption opt = doc->defaultTextOption();
            opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            doc->setDefaultTextOption(opt);
            doc->setTextWidth(w);
        }
        else
        {
            QTextOption opt;
            opt.setWrapMode(QTextOption::NoWrap);
            doc->setDefaultTextOption(opt);
            doc->setTextWidth(-1);
        }

        doc->adjustSize();

        int textH = int(std::ceil(doc->size().height()));
        textH = qMax(textH, fm.height());

        // Final width: use the actual document width (after wrapping decisions).
        // This avoids the "tiny horizontal scrollbar" caused by rounding/viewport mismatches.
        const int docW = qMax(1, int(std::ceil(doc->size().width())));
        const int finalW = qMin(docW, maxW);

        m_text->setFixedWidth(finalW);
        m_text->setFixedHeight(textH);
        if (m_bubbleBox)
        {
            m_bubbleBox->setFixedSize(finalW + 24, textH + 16);
            m_bubbleBox->updateGeometry();
        }

        m_text->updateGeometry();
    }

    bool m_isUser{false};
    QLabel* m_avatar{nullptr};
    QWidget* m_bubbleBox{nullptr};
    QTextBrowser* m_text{nullptr};
    int m_maxBubbleWidth{360};
    int m_rowWidth{520};
};

} // namespace

class ChatWindow::Impl {
public:
    QString modelFolder;
    QString modelDir;

    QListWidget* list{nullptr};
    EraTextEdit* input{nullptr};
    EraButton* sendBtn{nullptr};
    EraButton* clearBtn{nullptr};

    QPixmap userAvatar;
    QPixmap aiAvatar;

    ChatMessageWidget* currentAiBubble{nullptr};

    QJsonArray messages; // simplified format: {role, content}
    bool currentAiBubbleIsDraft{false};

    bool relayoutQueued{false};

    int streamingAssistantIndex{-1};

    void scheduleRelayout(QObject* context)
    {
        if (relayoutQueued) return;
        relayoutQueued = true;
        QMetaObject::invokeMethod(context, [this]{
            relayoutQueued = false;
            applyBubbleWidthForAll();
        }, Qt::QueuedConnection);
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
        const int maxW = computeBubbleMaxWidth();
        for (int i = 0; i < list->count(); ++i)
        {
            auto* item = list->item(i);
            if (!item) continue;
            if (auto* w = qobject_cast<ChatMessageWidget*>(list->itemWidget(item)))
            {
                w->setMaxBubbleWidth(maxW);
                w->setRowWidth(viewportRowWidth());
                const QSize hint = w->sizeHint();
                item->setSizeHint(QSize(viewportRowWidth(), hint.height()));
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

    void rebuildFromMessages()
    {
        list->clear();
        currentAiBubble = nullptr;

        const int maxW = computeBubbleMaxWidth();

        for (const auto& v : messages)
        {
            const auto o = v.toObject();
            const QString role = o.value("role").toString();
            const QString content = o.value("content").toString();
            const bool isUser = (role == "user");

            auto* item = new QListWidgetItem(list);
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);

            auto* bubble = new ChatMessageWidget(isUser ? userAvatar : aiAvatar, content, isUser);
            bubble->setMaxBubbleWidth(maxW);
            bubble->setRowWidth(viewportRowWidth());

            const QSize hint = bubble->sizeHint();
            item->setSizeHint(QSize(viewportRowWidth(), hint.height()));
            list->addItem(item);
            list->setItemWidget(item, bubble);
        }

        list->scrollToBottom();
        forceListRelayout();
    }

    void pushMessage(const QString& role, const QString& content)
    {
        QJsonObject o;
        o["role"] = role;
        o["content"] = content;
        messages.append(o);
        saveChatMessages(modelFolder, messages);
    }

    void beginStreamingAssistant()
    {
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
        saveChatMessages(modelFolder, messages);
    }

    void updateStreamingAssistantContent(const QString& content)
    {
        if (streamingAssistantIndex < 0 || streamingAssistantIndex >= messages.size()) return;
        auto o = messages.at(streamingAssistantIndex).toObject();
        if (o.value("role").toString() != QStringLiteral("assistant")) return;
        o["content"] = content;
        messages[streamingAssistantIndex] = o;
        saveChatMessages(modelFolder, messages);
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

    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    d->list = new QListWidget(central);
    d->list->setSpacing(6);
    d->list->setUniformItemSizes(false);
    d->list->setSelectionMode(QAbstractItemView::NoSelection);
    d->list->setFocusPolicy(Qt::NoFocus);
    d->list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    root->addWidget(d->list, 1);

    auto* bottom = new QWidget(central);
    auto* bl = new QHBoxLayout(bottom);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(8);

    d->input = new EraTextEdit(bottom);
    d->input->setPlaceholderText(tr("输入消息... (Enter 发送 / Shift+Enter 换行)"));
    d->input->setAcceptRichText(false);
    d->input->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    d->input->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    d->input->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    d->input->setMinimumHeight(32);
    d->input->setMaximumHeight(120);

    d->sendBtn = new EraButton(tr("发送"), bottom);
    d->sendBtn->setTone(EraButton::Tone::Link);
    d->clearBtn = new EraButton(tr("清除"), bottom);
    d->clearBtn->setTone(EraButton::Tone::Danger);

    bl->addWidget(d->input, 1);
    bl->addWidget(d->sendBtn);
    bl->addWidget(d->clearBtn);

    root->addWidget(bottom);

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
        if (!d->sendBtn->isEnabled()) return; // already busy/disabled

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

        // Optimistic disable to prevent rapid double-send before controller flips busy.
        d->sendBtn->setEnabled(false);
        d->input->setEnabled(false);

        emit requestSendMessage(d->modelFolder, text);
    };

    connect(d->sendBtn, &QPushButton::clicked, this, sendNow);
    connect(d->input, &EraTextEdit::sendRequested, this, sendNow);

    // Auto-grow the input box height with content up to maxHeight.
    connect(d->input->document(), &QTextDocument::contentsChanged, this, [this]{
        const int docH = int(std::ceil(d->input->document()->size().height()));
        const int target = qBound(32, docH + 6, 120);
        d->input->setFixedHeight(target);
    });

    connect(d->clearBtn, &QPushButton::clicked, this, [this]{
        if (d->modelFolder.isEmpty()) return;
        const auto ret = QMessageBox::question(this, tr("确认"), tr("确定要清除当前模型的聊天记录吗？"));
        if (ret != QMessageBox::Yes) return;
        emit requestClearChat(d->modelFolder);
    });

    // Responsive relayout on resize
    connect(d->list->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this]{
        d->applyBubbleWidthForAll();
    });

    // Also update widths when the viewport itself changes size (more immediate than rangeChanged)
    d->list->viewport()->installEventFilter(this);

    // Initial relayout after show; without this some bubbles get an incorrect first width.
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

    loadFromDisk(modelFolder);

    // After rebuilding, ensure widths match the current viewport size.
    d->scheduleRelayout(this);
}

void ChatWindow::setBusy(bool busy)
{
    d->sendBtn->setEnabled(!busy);
    d->input->setEnabled(!busy);
}

void ChatWindow::appendUserMessage(const QString& text)
{
    if (d->modelFolder.isEmpty()) return;
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
    d->beginStreamingAssistant();

    // UI-only draft bubble; the persisted content is updated via setAiMessageContent()/appendAiToken().
    d->currentAiBubbleIsDraft = true;

    if (d->list)
    {
        auto* item = new QListWidgetItem(d->list);
        auto* w = new ChatMessageWidget(d->aiAvatar, QString(), /*isUser*/false, nullptr);
        w->setMaxBubbleWidth(d->computeBubbleMaxWidth());
        w->setRowWidth(d->viewportRowWidth());
        const QSize hint = w->sizeHint();
        item->setSizeHint(QSize(d->viewportRowWidth(), hint.height()));
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
        return;

    d->currentAiBubble->appendToken(token);

    // Streaming: update the single persisted assistant placeholder, don't append new rows.
    if (!d->modelFolder.isEmpty())
        d->updateStreamingAssistantContent(d->currentAiBubble->content());

    // resize list item
    if (d->list)
    {
        auto* item = d->list->item(d->list->count() - 1);
        if (item)
        {
            const QSize hint = d->currentAiBubble->sizeHint();
            item->setSizeHint(QSize(d->viewportRowWidth(), hint.height()));
        }
        d->scheduleRelayout(this);
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
        d->updateStreamingAssistantContent(content);

        if (d->list)
        {
            auto* item = d->list->item(d->list->count() - 1);
            if (item)
            {
                const QSize hint = d->currentAiBubble->sizeHint();
                item->setSizeHint(QSize(d->viewportRowWidth(), hint.height()));
            }

            d->scheduleRelayout(this);
            d->list->scrollToBottom();
        }
    }
}

void ChatWindow::loadFromDisk(const QString& modelFolder)
{
    if (modelFolder.isEmpty()) return;
    d->messages = loadChatMessages(modelFolder);
    d->currentAiBubble = nullptr;
    d->currentAiBubbleIsDraft = false;
    d->streamingAssistantIndex = -1;
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
            if (d->sendBtn) d->sendBtn->setText(tr("发送"));
            if (d->clearBtn) d->clearBtn->setText(tr("清除"));
        }
    }

    if (e->type() == QEvent::Resize)
    {
        if (d && d->list)
        {
            d->applyBubbleWidthForAll();
        }
    }

    if (e->type() == QEvent::ApplicationPaletteChange || e->type() == QEvent::PaletteChange)
    {
        if (d)
        {
            if (d->list) d->list->viewport()->update();
            if (d->input) d->input->update();
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
            // Immediate relayout on viewport resize fixes the "right side empty" width glitch.
            d->applyBubbleWidthForAll();
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

    // Make sure we have a visible assistant bubble representing THIS reply.
    if (ensureBubbleExists)
    {
        if (!d->currentAiBubble)
            appendAiMessageStart();
        if (d->currentAiBubble)
            d->currentAiBubble->setContent(content);
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
                saveChatMessages(d->modelFolder, d->messages);
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
