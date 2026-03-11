#include "ui/SettingsWindow.hpp"
#include "common/SettingsManager.hpp"
#include "common/Utils.hpp"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QApplication>
#include <QPalette>
#include <QStackedWidget>
#include <QVBoxLayout>
#include "ui/era-style/EraTabBar.hpp"
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include "ui/era-style/EraComboBox.hpp"
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QDebug>
#include "ui/era-style/EraButton.hpp"
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QStyleFactory>
#include <QStyle>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QCheckBox>
#include "ui/era-style/EraSwitch.hpp"
#include <QScreen>
#include <QGuiApplication>
#include <QCursor>
#include <QLineEdit>
#include "ui/era-style/EraLineEdit.hpp"
#include <QPlainTextEdit>
#include "ui/era-style/EraPlainTextEdit.hpp"
#include <QLocale>
#include <QtGlobal>

#include <QtMultimedia/QMediaDevices>
#include <QtMultimedia/QAudioDevice>

// Helper copy (recursive)
static bool copyRecursively(const QString& srcPath, const QString& dstPath) {
    QDir src(srcPath);
    if (!src.exists()) return false;
    QDir dst(dstPath);
    if (!dst.exists()) { if (!dst.mkpath(".")) return false; }
    for (const QFileInfo& info : src.entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files | QDir::Hidden)) {
        QString rel = src.relativeFilePath(info.absoluteFilePath());
        QString to = dst.filePath(rel);
        if (info.isDir()) {
            if (!copyRecursively(info.absoluteFilePath(), to)) return false;
        } else {
            QDir toDir = QFileInfo(to).dir(); if (!toDir.exists()) toDir.mkpath(".");
            if (!QFile::copy(info.absoluteFilePath(), to)) {
                QFile::remove(to); // try overwrite
                if (!QFile::copy(info.absoluteFilePath(), to)) return false;
            }
        }
    }
    return true;
}

class SettingsWindow::Impl {
public:
    EraTabBar*     tabs{nullptr};
    QStackedWidget* tabStack{nullptr};
    QWidget* basic{nullptr};
    QFormLayout* basicForm{nullptr};
    QWidget* pathRowWidget{nullptr};
    QWidget* topRowWidget{nullptr};
    EraComboBox* modelCombo{nullptr};
    QLabel* pathLabel{nullptr};
    EraButton* chooseBtn{nullptr};
    EraButton* openBtn{nullptr};
    EraButton* resetDirBtn{nullptr};
    EraComboBox* themeCombo{nullptr};
    EraComboBox* languageCombo{nullptr};
    EraButton* resetBtn{nullptr};

    QWidget* modelTab{nullptr};
    QLabel* modelNameTitle{nullptr};
    QLabel* watermarkTitle{nullptr};
    QLabel* curModelName{nullptr};
    EraButton* openModelDirBtn{nullptr};
    EraSwitch* chkBlink{nullptr};
    EraSwitch* chkBreath{nullptr};
    EraSwitch* chkGaze{nullptr};
    EraSwitch* chkPhysics{nullptr};
    QLabel* tipBreath{nullptr};
    QLabel* tipBlink{nullptr};
    QLabel* tipGaze{nullptr};
    QLabel* tipPhysics{nullptr};

    QLabel* wmFileLabel{nullptr};
    EraButton* wmChooseBtn{nullptr};
    EraButton* wmClearBtn{nullptr};

    // AI tab
    QWidget* aiTab{nullptr};
    QFormLayout* aiForm{nullptr};
    EraLineEdit* aiBaseUrl{nullptr};
    EraLineEdit* aiKey{nullptr};
    EraLineEdit* aiModel{nullptr};
    EraPlainTextEdit* aiSystemPrompt{nullptr};
    EraSwitch* aiStream{nullptr};
    QWidget* aiBaseUrlRow{nullptr};
    QWidget* aiKeyRow{nullptr};
    QWidget* aiModelRow{nullptr};
    QWidget* aiSystemPromptRow{nullptr};
    QWidget* ttsBaseUrlRow{nullptr};
    QWidget* ttsKeyRow{nullptr};
    QWidget* ttsModelRow{nullptr};
    QWidget* ttsVoiceRow{nullptr};
    QLabel* tipAiBaseUrl{nullptr};
    QLabel* tipAiKey{nullptr};
    QLabel* tipAiModel{nullptr};
    QLabel* tipAiSystemPrompt{nullptr};
    QLabel* tipAiStream{nullptr};
    QLabel* tipTtsBaseUrl{nullptr};
    QLabel* tipTtsKey{nullptr};
    QLabel* tipTtsModel{nullptr};
    QLabel* tipTtsVoice{nullptr};

    // TTS
    EraLineEdit* ttsBaseUrl{nullptr};
    EraLineEdit* ttsKey{nullptr};
    EraLineEdit* ttsModel{nullptr};
    EraLineEdit* ttsVoice{nullptr};

    QWidget* advancedTab{nullptr};
    QFormLayout* advancedForm{nullptr};
    QWidget* cleanupRowWidget{nullptr};
    EraButton* clearCacheBtn{nullptr};
    EraButton* clearChatsBtn{nullptr};
    EraComboBox* texCapCombo{nullptr};
    EraComboBox* msaaCombo{nullptr};

    // Advanced: new combos
    EraComboBox* screenCombo{nullptr};
    EraComboBox* audioOutCombo{nullptr};

    QFileSystemWatcher* fsw{nullptr};
    QTimer* debounce{nullptr};
    QWidget* central{nullptr};
};

SettingsWindow::SettingsWindow(QWidget *parent) : QMainWindow(parent), d(new Impl) {
    setWindowTitle(tr("设置"));
    setWindowFlag(Qt::Window, true);
    setWindowFlag(Qt::CustomizeWindowHint, false);
    setWindowFlag(Qt::WindowMaximizeButtonHint, false);
    resize(520, 420);

    QScreen* s = nullptr;
    if (parentWidget() && parentWidget()->screen()) s = parentWidget()->screen();
    if (!s) s = QGuiApplication::screenAt(QCursor::pos());
    if (!s) s = QGuiApplication::primaryScreen();
    if (s) {
        QRect scr = s->availableGeometry();
        int w = width(); int h = height();
        move(scr.x() + (scr.width() - w)/2, scr.y() + (scr.height() - h)/2);
    }

    d->central = new QWidget(this);
    setCentralWidget(d->central);

    auto rootLay = new QVBoxLayout(d->central);
    rootLay->setContentsMargins(12,12,12,12);
    d->tabs = new EraTabBar(d->central);
    d->tabStack = new QStackedWidget(d->central);
    rootLay->addWidget(d->tabs);
    rootLay->addWidget(d->tabStack, 1);
    connect(d->tabs, &EraTabBar::currentChanged, this, [this](int index){
        d->tabStack->setCurrentIndex(index);
        if (auto* fw = QApplication::focusWidget()) fw->clearFocus();
        d->tabs->setFocus(Qt::OtherFocusReason);
    });

    // Basic tab
    d->basic = new QWidget(d->tabStack);
    auto form = new QFormLayout(d->basic);
    d->basicForm = form;

    // Model path row
    auto pathRow = new QWidget(d->basic);
    d->pathRowWidget = pathRow;
    auto hl = new QHBoxLayout(pathRow); hl->setContentsMargins(0,0,0,0);
    d->pathLabel = new QLabel(SettingsManager::instance().modelsRoot(), pathRow);
    d->chooseBtn = new EraButton(tr("选择路径"), pathRow);
    d->chooseBtn->setTone(EraButton::Tone::Link);
    d->openBtn   = new EraButton(tr("打开路径"), pathRow);
    d->openBtn->setTone(EraButton::Tone::Link);
    d->resetDirBtn = new EraButton(tr("恢复默认"), pathRow);
    d->resetDirBtn->setTone(EraButton::Tone::Neutral);
    hl->addWidget(d->pathLabel, 1);
    hl->addWidget(d->chooseBtn);
    hl->addWidget(d->openBtn);
    hl->addWidget(d->resetDirBtn);
    form->addRow(tr("模型路径："), pathRow);

    // Current model row
    d->modelCombo = new EraComboBox(d->basic);
    d->resetBtn = new EraButton(tr("还原初始状态"), d->basic);
    d->resetBtn->setTone(EraButton::Tone::Danger);
    auto topRow = new QWidget(d->basic);
    d->topRowWidget = topRow;
    auto topLay = new QHBoxLayout(topRow); topLay->setContentsMargins(0,0,0,0);
    topLay->addWidget(d->modelCombo, 1);
    topLay->addWidget(d->resetBtn);
    form->addRow(tr("当前模型："), topRow);

    // Theme
    d->themeCombo = new EraComboBox(d->basic);
    // 仅保留“跟随系统”，不再手动切换亮/暗，也不在代码里控制 palette
    d->themeCombo->addItems({tr("跟随系统")});
    d->themeCombo->setEnabled(false);
    form->addRow(tr("当前主题："), d->themeCombo);

    d->languageCombo = new EraComboBox(d->basic);
    d->languageCombo->addItem(tr("跟随系统"), QStringLiteral("system"));
    d->languageCombo->addItem(tr("简体中文"), QStringLiteral("zh_CN"));
    d->languageCombo->addItem(QStringLiteral("English"), QStringLiteral("en_US"));
    {
        const QString saved = SettingsManager::instance().currentLanguage();
        const QString sysCode = QLocale::system().name().startsWith("zh", Qt::CaseInsensitive)
                                    ? QStringLiteral("zh_CN")
                                    : QStringLiteral("en_US");
        if (saved == sysCode) d->languageCombo->setCurrentIndex(0);
        else {
            const int idx = d->languageCombo->findData(saved);
            d->languageCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        }
    }
    form->addRow(tr("当前语言："), d->languageCombo);

    d->tabStack->addWidget(d->basic);
    d->tabs->addTab(tr("基本设置"));

    // Model settings tab
    d->modelTab = new QWidget(d->tabStack);
    auto vbox = new QVBoxLayout(d->modelTab);
    auto row1 = new QHBoxLayout(); row1->setSpacing(6); row1->setContentsMargins(0,0,0,0);
    d->curModelName = new QLabel("", d->modelTab);
    d->openModelDirBtn = new EraButton(tr("打开当前模型路径"), d->modelTab);
    d->openModelDirBtn->setTone(EraButton::Tone::Link);
    d->modelNameTitle = new QLabel(tr("模型名称："), d->modelTab);
    row1->addWidget(d->modelNameTitle);
    row1->addWidget(d->curModelName, 1);
    row1->addWidget(d->openModelDirBtn);
    vbox->addLayout(row1);

    auto wmRow = new QHBoxLayout(); wmRow->setSpacing(6); wmRow->setContentsMargins(0,0,0,0);
    d->watermarkTitle = new QLabel(tr("去除水印："), d->modelTab);
    wmRow->addWidget(d->watermarkTitle);
    d->wmFileLabel = new QLabel(tr("无"), d->modelTab);
    d->wmFileLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    d->wmChooseBtn = new EraButton(tr("选择文件"), d->modelTab);
    d->wmChooseBtn->setTone(EraButton::Tone::Link);
    d->wmClearBtn  = new EraButton(tr("取消所选"), d->modelTab);
    d->wmClearBtn->setTone(EraButton::Tone::Neutral);
    wmRow->addWidget(d->wmFileLabel, 1);
    wmRow->addWidget(d->wmChooseBtn);
    wmRow->addWidget(d->wmClearBtn);
    vbox->addLayout(wmRow);

    d->chkBreath  = new EraSwitch(tr("自动呼吸"), d->modelTab);
    d->chkBlink   = new EraSwitch(tr("自动眨眼"), d->modelTab);
    d->chkGaze    = new EraSwitch(tr("视线跟踪"), d->modelTab);
    d->chkPhysics = new EraSwitch(tr("物理模拟"), d->modelTab);

    d->chkBreath->setChecked(SettingsManager::instance().enableBreath());
    d->chkBlink->setChecked(SettingsManager::instance().enableBlink());
    d->chkGaze->setChecked(SettingsManager::instance().enableGaze());
    d->chkPhysics->setChecked(SettingsManager::instance().enablePhysics());

    auto mkInlineTipRow = [this](EraSwitch* chk, const QString& tip, QLabel** outTip){
        QWidget* row = new QWidget(d->modelTab);
        auto lay = new QHBoxLayout(row);
        lay->setContentsMargins(0,0,0,0); lay->setSpacing(4);
        chk->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
        lay->addWidget(chk);
        lay->addWidget(new QLabel(" ", row));
        auto tipLbl = new QLabel(QString::fromUtf8("ⓘ"), row);
        tipLbl->setToolTip(tip);
        tipLbl->setCursor(Qt::WhatsThisCursor);
        if (outTip) *outTip = tipLbl;
        lay->addWidget(tipLbl);
        lay->addStretch(1);
        return row;
    };

    vbox->addWidget(mkInlineTipRow(d->chkBreath,  tr("让角色在静止时也会轻微起伏（身体角度/呼吸参数）。开启视线跟踪时，自动呼吸不再影响头部；关闭后恢复。关闭本项后参数会复位。"), &d->tipBreath));
    vbox->addWidget(mkInlineTipRow(d->chkBlink,   tr("自动眨眼并保留更自然的间隔与瞬目效果。关闭后眼部相关参数复位。"), &d->tipBlink));
    vbox->addWidget(mkInlineTipRow(d->chkGaze,    tr("眼球、头部与身体随鼠标方向轻微转动，远距离时幅度会衰减。默认关闭；开启后将屏蔽自动呼吸对头部的影响。关闭后恢复眼球微动策略。"), &d->tipGaze));
    vbox->addWidget(mkInlineTipRow(d->chkPhysics, tr("根据模型 physics3.json 的配置驱动物理（如头发/衣物摆动）。关闭后复位受影响参数。"), &d->tipPhysics));
    vbox->addStretch(1);
    d->tabStack->addWidget(d->modelTab);
    d->tabs->addTab(tr("模型设置"));

    // ---- AI tab ----
    d->aiTab = new QWidget(d->tabStack);
    {
        auto lay = new QVBoxLayout(d->aiTab);
        auto form2 = new QFormLayout();
        d->aiForm = form2;

        d->aiBaseUrl = new EraLineEdit(SettingsManager::instance().aiBaseUrl(), d->aiTab);
        d->aiBaseUrl->setFixedHeight(26);
        d->aiKey = new EraLineEdit(SettingsManager::instance().aiApiKey(), d->aiTab);
        d->aiKey->setFixedHeight(26);
        d->aiKey->setEchoMode(QLineEdit::Password);
        d->aiModel = new EraLineEdit(SettingsManager::instance().aiModel(), d->aiTab);
        d->aiModel->setFixedHeight(26);
        d->aiSystemPrompt = new EraPlainTextEdit(SettingsManager::instance().aiSystemPrompt(), d->aiTab);
        d->aiSystemPrompt->setPlaceholderText(tr("支持变量：$name$（当前模型名）"));
        d->aiStream = new EraSwitch(tr("是否流式输出"), d->aiTab);
        d->aiStream->setChecked(SettingsManager::instance().aiStreamEnabled());

        // AI/TTS tooltip helper (same symbol style as 模型设置)
        auto mkInlineInfo = [](const QString& tip, QWidget* parent, QLabel** outTip){
            QWidget* w = new QWidget(parent);
            auto hl = new QHBoxLayout(w);
            hl->setContentsMargins(0,0,0,0);
            hl->setSpacing(2);
            hl->addStretch(1);
            hl->addWidget(new QLabel(" ", w));
            auto tipLbl = new QLabel(QString::fromUtf8("ⓘ"), w);
            tipLbl->setToolTip(tip);
            tipLbl->setCursor(Qt::WhatsThisCursor);
            if (outTip) *outTip = tipLbl;
            hl->addWidget(tipLbl);
            return w;
        };

        auto mkRowWithInfo = [&](const QString& label, QWidget* field, const QString& tip, QWidget** outRow, QLabel** outTip){
            QWidget* row = new QWidget(d->aiTab);
            auto hl = new QHBoxLayout(row);
            hl->setContentsMargins(0,0,0,0);
            hl->setSpacing(6);
            hl->addWidget(field, 1);
            hl->addWidget(mkInlineInfo(tip, row, outTip));
            form2->addRow(label, row);
            if (outRow) *outRow = row;
        };

        // 对话配置
        mkRowWithInfo(
            tr("对话API："),
            d->aiBaseUrl,
            tr(
                "填写 OpenAI 兼容接口的 Base URL。\n"
                "例： https://example.com/v1 （只填到 /v1 即可，程序会自动补全为 /v1/chat/completions）\n\n"
                "对应 OpenAI 参数：请求地址（endpoint）。\n"
                "用于：Chat Completions（/chat/completions）。"
            ),
            &d->aiBaseUrlRow,
            &d->tipAiBaseUrl
        );
        mkRowWithInfo(
            tr("对话KEY："),
            d->aiKey,
            tr(
                "API Key（可留空）。\n"
                "通常是 Bearer Token：Authorization: Bearer <api_key>。\n\n"
                "对应 OpenAI 参数：请求头 Authorization。\n"
                "留空时将不发送 Authorization 头（适用于本地/内网免鉴权服务）。"
            ),
            &d->aiKeyRow,
            &d->tipAiKey
        );
        mkRowWithInfo(
            tr("对话模型："),
            d->aiModel,
            tr(
                "要使用的对话模型名称。\n"
                "例：gpt-4o-mini、qwen-plus、deepseek-chat 等（取决于你的服务端支持）。\n\n"
                "对应 OpenAI 参数：model。"
            ),
            &d->aiModelRow,
            &d->tipAiModel
        );

        // 对话人设（system prompt） + tooltip
        {
            QWidget* row = new QWidget(d->aiTab);
            auto hl = new QHBoxLayout(row);
            hl->setContentsMargins(0,0,0,0);
            hl->setSpacing(6);
            hl->addWidget(d->aiSystemPrompt, 1);
            hl->addWidget(mkInlineInfo(
                tr(
                    "System Prompt（系统提示词 / 人设），用于规定 AI 的角色、语气、规则与边界。\n\n"
                    "对应 OpenAI 参数：messages[0].role = \"system\" 的 content。\n\n"
                    "支持变量：\n"
                    "  • $name$：会在发送前替换成“当前模型名称”。\n\n"
                    "示例：\n"
                    "你是 $name$，是一只桌宠。回答要简短、温柔，并尽量使用口语。"
                ), row, &d->tipAiSystemPrompt));
            d->aiSystemPromptRow = row;
            form2->addRow(tr("对话人设："), row);
        }

        // 是否流式输出
        {
            QWidget* row = new QWidget(d->aiTab);
            auto hl = new QHBoxLayout(row);
            hl->setContentsMargins(0,0,0,0);
            hl->setSpacing(6);
            hl->addWidget(d->aiStream);
            hl->addWidget(mkInlineInfo(
                tr(
                    "开启后，AI 回复会“边生成边显示”（更像实时打字）。\n"
                    "关闭后，会等完整回复生成后一次性显示。\n\n"
                    "对应 OpenAI 参数：stream（true/false）。\n"
                    "提示：启用语音时，仍建议开启流式输出以获得更自然的对话节奏。"
                ), row, &d->tipAiStream));
            hl->addStretch(1);
            form2->addRow(QString(), row);
        }

        // TTS fields
        d->ttsBaseUrl = new EraLineEdit(SettingsManager::instance().ttsBaseUrl(), d->aiTab);
        d->ttsBaseUrl->setFixedHeight(26);
        d->ttsKey = new EraLineEdit(SettingsManager::instance().ttsApiKey(), d->aiTab);
        d->ttsKey->setFixedHeight(26);
        d->ttsKey->setEchoMode(QLineEdit::Password);
        d->ttsModel = new EraLineEdit(SettingsManager::instance().ttsModel(), d->aiTab);
        d->ttsModel->setFixedHeight(26);
        d->ttsVoice = new EraLineEdit(SettingsManager::instance().ttsVoice(), d->aiTab);
        d->ttsVoice->setFixedHeight(26);

        mkRowWithInfo(
            tr("语音API："),
            d->ttsBaseUrl,
            tr(
                "填写 OpenAI 兼容 TTS 接口的 Base URL。\n"
                "例： https://example.com/v1 （只填到 /v1 即可，程序会自动补全为 /v1/audio/speech）\n\n"
                "对应 OpenAI 参数：请求地址（endpoint）。\n"
                "用于：Text-to-Speech（/audio/speech）。\n\n"
                "留空表示禁用语音：只显示文字，不请求语音。"
            ),
            &d->ttsBaseUrlRow,
            &d->tipTtsBaseUrl
        );
        mkRowWithInfo(
            tr("语音KEY："),
            d->ttsKey,
            tr(
                "TTS 的 API Key（可留空）。\n"
                "对应 OpenAI 参数：请求头 Authorization。\n"
                "留空时将不发送 Authorization 头。"
            ),
            &d->ttsKeyRow,
            &d->tipTtsKey
        );
        mkRowWithInfo(
            tr("语音模型："),
            d->ttsModel,
            tr(
                "TTS 模型名称。\n"
                "例：tts-1、gpt-4o-mini-tts 等（取决于服务端支持）。\n\n"
                "对应 OpenAI 参数：model。"
            ),
            &d->ttsModelRow,
            &d->tipTtsModel
        );
        mkRowWithInfo(
            tr("语音音色："),
            d->ttsVoice,
            tr(
                "语音音色（voice）。\n"
                "不同服务端的可用值不同，常见如：alloy、verse、aria 等。\n\n"
                "对应 OpenAI 参数：voice。"
            ),
            &d->ttsVoiceRow,
            &d->tipTtsVoice
        );

        lay->addLayout(form2);
        lay->addStretch(1);

        // hot update
        connect(d->aiBaseUrl, &QLineEdit::textChanged, this, [](const QString& t){ SettingsManager::instance().setAiBaseUrl(t); });
        connect(d->aiKey, &QLineEdit::textChanged, this, [](const QString& t){ SettingsManager::instance().setAiApiKey(t); });
        connect(d->aiModel, &QLineEdit::textChanged, this, [](const QString& t){ SettingsManager::instance().setAiModel(t); });
        connect(d->aiSystemPrompt, &QPlainTextEdit::textChanged, this, [this]{ SettingsManager::instance().setAiSystemPrompt(d->aiSystemPrompt->toPlainText()); });
        connect(d->aiStream, &EraSwitch::toggled, this, [](bool on){ SettingsManager::instance().setAiStreamEnabled(on); });

        connect(d->ttsBaseUrl, &QLineEdit::textChanged, this, [](const QString& t){ SettingsManager::instance().setTtsBaseUrl(t); });
        connect(d->ttsKey, &QLineEdit::textChanged, this, [](const QString& t){ SettingsManager::instance().setTtsApiKey(t); });
        connect(d->ttsModel, &QLineEdit::textChanged, this, [](const QString& t){ SettingsManager::instance().setTtsModel(t); });
        connect(d->ttsVoice, &QLineEdit::textChanged, this, [](const QString& t){ SettingsManager::instance().setTtsVoice(t); });
    }
    d->tabStack->addWidget(d->aiTab);
    d->tabs->addTab(tr("AI设置"));

    // Advanced tab
    d->advancedTab = new QWidget(d->tabStack);
    auto advLay = new QFormLayout(d->advancedTab);
    d->advancedForm = advLay;
    d->texCapCombo = new EraComboBox(d->advancedTab); d->texCapCombo->addItems({"4096","3072","2048","1024"});
    {
        int cur = SettingsManager::instance().textureMaxDim();
        int idx = d->texCapCombo->findText(QString::number(cur)); if (idx<0) idx = 2; d->texCapCombo->setCurrentIndex(idx);
    }
    advLay->addRow(tr("贴图上限："), d->texCapCombo);
    d->msaaCombo = new EraComboBox(d->advancedTab); d->msaaCombo->addItems({"2x","4x","8x"});
    {
        int cur = SettingsManager::instance().msaaSamples();
        int idx = (cur==2?0:(cur==8?2:1)); d->msaaCombo->setCurrentIndex(idx);
    }
    advLay->addRow(tr("MSAA："), d->msaaCombo);

    // ---- Model display screen ----
    d->screenCombo = new EraComboBox(d->advancedTab);
    d->screenCombo->addItem(tr("系统默认（默认）"), QString());
    {
        const QString preferred = SettingsManager::instance().preferredScreenName();
        const QList<QScreen*> screens = QGuiApplication::screens();
        for (QScreen* s : screens)
        {
            const QString name = s ? s->name() : QString();
            if (name.isEmpty()) continue;
            // If no preferred, mark the current primary as default.
            const bool isDefault = (QGuiApplication::primaryScreen() == s);
            const QString label = isDefault ? (name + tr("（默认）")) : name;
            d->screenCombo->addItem(label, name);
        }
        // select
        if (!preferred.isEmpty())
        {
            const int idx = d->screenCombo->findData(preferred);
            if (idx >= 0) d->screenCombo->setCurrentIndex(idx);
        }
        else
        {
            d->screenCombo->setCurrentIndex(0);
        }
    }
    advLay->addRow(tr("模型显示："), d->screenCombo);

    // ---- Audio output device ----
    d->audioOutCombo = new EraComboBox(d->advancedTab);
    d->audioOutCombo->addItem(tr("系统默认（默认）"), QString());
    {
        const QString preferredB64 = SettingsManager::instance().preferredAudioOutputIdBase64();
        const auto defDev = QMediaDevices::defaultAudioOutput();
        const QList<QAudioDevice> devs = QMediaDevices::audioOutputs();
        for (const QAudioDevice& dev : devs)
        {
            const QByteArray id = dev.id();
            const QString idB64 = QString::fromLatin1(id.toBase64());
            const bool isDefault = (defDev.id() == id);
            const QString label = isDefault ? (dev.description() + tr("（默认）")) : dev.description();
            d->audioOutCombo->addItem(label, idB64);
        }
        if (!preferredB64.isEmpty())
        {
            const int idx = d->audioOutCombo->findData(preferredB64);
            if (idx >= 0) d->audioOutCombo->setCurrentIndex(idx);
            else d->audioOutCombo->setCurrentIndex(0);
        }
        else
        {
            d->audioOutCombo->setCurrentIndex(0);
        }
    }
    advLay->addRow(tr("音频输出："), d->audioOutCombo);

    // 清理按钮
    auto cleanupRow = new QWidget(d->advancedTab);
    d->cleanupRowWidget = cleanupRow;
    auto cleanupLay = new QHBoxLayout(cleanupRow);
    cleanupLay->setContentsMargins(0, 0, 0, 0);
    auto btnClearCache = new EraButton(tr("清除缓存"), cleanupRow);
    btnClearCache->setTone(EraButton::Tone::Warning);
    auto btnClearChats = new EraButton(tr("清除所有对话历史"), cleanupRow);
    btnClearChats->setTone(EraButton::Tone::Danger);
    d->clearCacheBtn = btnClearCache;
    d->clearChatsBtn = btnClearChats;
    cleanupLay->addWidget(d->clearCacheBtn);
    cleanupLay->addWidget(d->clearChatsBtn);
    cleanupLay->addStretch(1);
    advLay->addRow(tr("清理："), cleanupRow);

    advLay->addItem(new QSpacerItem(0,0,QSizePolicy::Minimum,QSizePolicy::Expanding));
    d->tabStack->addWidget(d->advancedTab);
    d->tabs->addTab(tr("高级设置"));

    auto confirm = [this](const QString& title, const QString& text) -> bool {
        auto ret = QMessageBox::question(this, title, text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        return ret == QMessageBox::Yes;
    };

    connect(btnClearCache, &QPushButton::clicked, this, [this, confirm]{
        if (!confirm(tr("确认清除缓存"), tr("将删除缓存目录（.Cache）下的所有文件。确定继续吗？"))) return;
        const QString cacheDir = SettingsManager::instance().cacheDir();
        QDir dir(cacheDir);
        if (!dir.exists()) dir.mkpath(".");
        const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        for (const auto& fi : files) {
            QFile::remove(fi.absoluteFilePath());
        }
        const QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto& di : subDirs) {
            QDir(di.absoluteFilePath()).removeRecursively();
        }
        QMessageBox::information(this, tr("完成"), tr("缓存已清除。"));
    });

    connect(btnClearChats, &QPushButton::clicked, this, [this, confirm]{
        if (!confirm(tr("确认清除所有对话历史"), tr("将清空当前聊天窗口的记录，并删除 Chats 目录下的所有会话文件。确定继续吗？"))) return;
        // ensure chats dir exists then remove all files
        const QString chatsDir = SettingsManager::instance().chatsDir();
        QDir dir(chatsDir);
        if (!dir.exists()) dir.mkpath(".");
        const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        for (const auto& fi : files) {
            QFile::remove(fi.absoluteFilePath());
        }
        const QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto& di : subDirs) {
            QDir(di.absoluteFilePath()).removeRecursively();
        }
        // 清空当前聊天窗口显示（只影响当前窗口，不触发模型切换逻辑）
        emit requestClearAllChats();
        QMessageBox::information(this, tr("完成"), tr("所有对话历史已清除。"));
    });

    // Watcher for dynamic model folder changes
    d->fsw = new QFileSystemWatcher(this);
    d->debounce = new QTimer(this); d->debounce->setSingleShot(true); d->debounce->setInterval(250);
    auto scheduleRefresh = [this]{ d->debounce->start(); };
    connect(d->debounce, &QTimer::timeout, this, [this]{
        QString curText = d->modelCombo->currentText();
        refreshModelList();
        int idx = d->modelCombo->findText(curText); if (idx >= 0) d->modelCombo->setCurrentIndex(idx);
        QString wm = SettingsManager::instance().watermarkExpPath();
        d->wmFileLabel->setText(wm.isEmpty()? tr("无") : QFileInfo(wm).fileName());
        emit watermarkChanged(wm);
    });
    connect(d->fsw, &QFileSystemWatcher::directoryChanged, this, [scheduleRefresh](const QString&){ scheduleRefresh(); });

    refreshModelList();

    // Init state from SettingsManager
    {
        auto& sm = SettingsManager::instance();
        QString folder = sm.selectedModelFolder(); if (!folder.isEmpty()) sm.ensureModelConfigExists(folder);
        d->chkBreath->setChecked(sm.enableBreath());
        d->chkBlink->setChecked(sm.enableBlink());
        d->chkGaze->setChecked(sm.enableGaze());
        d->chkPhysics->setChecked(sm.enablePhysics());
        QString wm = sm.watermarkExpPath(); d->wmFileLabel->setText(wm.isEmpty()? tr("无") : QFileInfo(wm).fileName());
    }

    // Connections
    connect(d->modelCombo, &QComboBox::currentTextChanged, this, [this](const QString& name){
        if (name.isEmpty()) return;
        auto& sm = SettingsManager::instance();
        if (sm.selectedModelFolder() == name) return;
        sm.setSelectedModelFolder(name);
        sm.ensureModelConfigExists(name);
        for (const auto& e : sm.scanModels())
        {
            if (e.folderName == name)
            {
                qDebug() << "[ModelFlow][SettingsWindow] emit requestLoadModel" << "name=" << name << "json=" << e.jsonPath;
                emit requestLoadModel(e.jsonPath);
                break;
            }
        }
        d->chkBreath->setChecked(SettingsManager::instance().enableBreath());
        d->chkBlink->setChecked(SettingsManager::instance().enableBlink());
        d->chkGaze->setChecked(SettingsManager::instance().enableGaze());
        d->chkPhysics->setChecked(SettingsManager::instance().enablePhysics());

        const QString wm = SettingsManager::instance().watermarkExpPath();
        d->wmFileLabel->setText(wm.isEmpty() ? tr("无") : QFileInfo(wm).fileName());
        emit watermarkChanged(wm);
    });

    auto chooseExistingDirectory = [this](const QString& title, const QString& startDir) -> QString {
        QFileDialog dlg(this, title, startDir);
        dlg.setFileMode(QFileDialog::Directory);
        dlg.setOption(QFileDialog::ShowDirsOnly, true);
#if defined(Q_OS_LINUX)
        dlg.setOption(QFileDialog::DontUseNativeDialog, true);
#endif
        if (dlg.exec() != QDialog::Accepted) return {};
        const QStringList files = dlg.selectedFiles();
        return files.isEmpty() ? QString() : files.first();
    };

    auto chooseOpenFile = [this](const QString& title, const QString& startDir, const QString& filter) -> QString {
        QFileDialog dlg(this, title, startDir, filter);
        dlg.setFileMode(QFileDialog::ExistingFile);
#if defined(Q_OS_LINUX)
        dlg.setOption(QFileDialog::DontUseNativeDialog, true);
#endif
        if (dlg.exec() != QDialog::Accepted) return {};
        const QStringList files = dlg.selectedFiles();
        return files.isEmpty() ? QString() : files.first();
    };

    connect(d->chooseBtn, &QPushButton::clicked, this, [this, chooseExistingDirectory]{
        QString dir = chooseExistingDirectory(tr("选择模型根目录"), SettingsManager::instance().modelsRoot());
        if (dir.isEmpty()) return; SettingsManager::instance().setModelsRoot(dir); d->pathLabel->setText(dir); refreshModelList();
    });
    connect(d->openBtn, &QPushButton::clicked, this, [this]{ QDesktopServices::openUrl(QUrl::fromLocalFile(SettingsManager::instance().modelsRoot())); });
    connect(d->resetDirBtn, &QPushButton::clicked, this, [this]{ auto& sm = SettingsManager::instance(); sm.resetModelsRootToDefault(QCoreApplication::applicationDirPath()); d->pathLabel->setText(sm.modelsRoot()); refreshModelList(); });

    connect(d->resetBtn, &QPushButton::clicked, this, [this]{ emit requestResetWindow(); });

    auto refreshCurModelName = [this]{
        QString name = SettingsManager::instance().selectedModelFolder();
        if (name.isEmpty()) {
            auto entries = SettingsManager::instance().scanModels();
            int idx = -1; for (int i=0;i<entries.size();++i) if (entries[i].folderName.compare("Hiyori", Qt::CaseInsensitive)==0){ idx=i; break; }
            if (idx >= 0) name = entries[idx].folderName; else if (!entries.isEmpty()) name = entries.front().folderName;
        }
        d->curModelName->setText(name.isEmpty()? tr("(无)") : name);
        QString wm = SettingsManager::instance().watermarkExpPath(); d->wmFileLabel->setText(wm.isEmpty()? tr("无") : QFileInfo(wm).fileName());
    };
    refreshCurModelName();
    connect(d->openModelDirBtn, &QPushButton::clicked, this, [this]{ QString folder = SettingsManager::instance().selectedModelFolder(); QString root = SettingsManager::instance().modelsRoot(); if (folder.isEmpty()) { auto entries = SettingsManager::instance().scanModels(); if (!entries.isEmpty()) folder = entries.front().folderName; } if (!folder.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(root).filePath(folder))); });
    connect(d->modelCombo, &QComboBox::currentTextChanged, this, [refreshCurModelName](const QString&){ refreshCurModelName(); });

    connect(d->chkBlink, &EraSwitch::toggled, this, [this](bool on){ SettingsManager::instance().setEnableBlink(on); emit toggleBlink(on); });
    connect(d->chkBreath, &EraSwitch::toggled, this, [this](bool on){ SettingsManager::instance().setEnableBreath(on); emit toggleBreath(on); });
    connect(d->chkGaze, &EraSwitch::toggled, this, [this](bool on){ SettingsManager::instance().setEnableGaze(on); emit toggleGaze(on); });
    connect(d->chkPhysics, &EraSwitch::toggled, this, [this](bool on){ SettingsManager::instance().setEnablePhysics(on); emit togglePhysics(on); });

    connect(d->wmChooseBtn, &QPushButton::clicked, this, [this, chooseOpenFile]{ QString folder = SettingsManager::instance().selectedModelFolder(); if (folder.isEmpty()) return; QString root = SettingsManager::instance().modelsRoot(); QString modelDir = QDir(root).filePath(folder); QString path = chooseOpenFile(tr("选择水印表达式文件"), modelDir, "Expression (*.exp3.json)"); if (path.isEmpty()) return; SettingsManager::instance().setWatermarkExpPath(path); d->wmFileLabel->setText(QFileInfo(path).fileName()); emit watermarkChanged(path); });
    connect(d->wmClearBtn, &QPushButton::clicked, this, [this]{ SettingsManager::instance().setWatermarkExpPath(""); d->wmFileLabel->setText(tr("无")); emit watermarkChanged(""); });

    connect(d->modelCombo, &QComboBox::currentTextChanged, this, [this](const QString&){ auto& sm = SettingsManager::instance(); int curCap = sm.textureMaxDim(); int idxCap = d->texCapCombo->findText(QString::number(curCap)); if (idxCap>=0) d->texCapCombo->setCurrentIndex(idxCap); int curMsaa = sm.msaaSamples(); int idxMsaa = (curMsaa==2?0:(curMsaa==8?2:1)); d->msaaCombo->setCurrentIndex(idxMsaa); });

    connect(d->texCapCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int){ int dim = d->texCapCombo->currentText().toInt(); SettingsManager::instance().setTextureMaxDim(dim); emit textureCapChanged(dim); });
    connect(d->msaaCombo,  qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int){ QString t = d->msaaCombo->currentText(); int samples = t.startsWith("2")?2:(t.startsWith("8")?8:4); SettingsManager::instance().setMsaaSamples(samples); emit msaaChanged(samples); });

    // Advanced: screen / audio output hot update
    connect(d->screenCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int){
        const QString name = d->screenCombo->currentData().toString();
        SettingsManager::instance().setPreferredScreenName(name);
        emit preferredScreenChanged(name);
    });
    connect(d->audioOutCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int){
        const QString idB64 = d->audioOutCombo->currentData().toString();
        SettingsManager::instance().setPreferredAudioOutputIdBase64(idB64);
        emit preferredAudioOutputChanged(idB64);
    });

    connect(d->languageCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int){
        const QString sel = d->languageCombo->currentData().toString();
        QString code;
        if (sel == QStringLiteral("system")) {
            code = QLocale::system().name().startsWith("zh", Qt::CaseInsensitive)
                       ? QStringLiteral("zh_CN")
                       : QStringLiteral("en_US");
        } else {
            code = sel;
        }
        SettingsManager::instance().setCurrentLanguage(code);
        emit languageChanged(code);
    });

    // ---- AI connections ----
    auto emitAiChanged = [this]{ emit aiSettingsChanged(); };

    connect(d->aiBaseUrl, &QLineEdit::editingFinished, this, [this, emitAiChanged]{
        SettingsManager::instance().setAiBaseUrl(d->aiBaseUrl->text());
        emitAiChanged();
    });
    connect(d->aiKey, &QLineEdit::editingFinished, this, [this, emitAiChanged]{
        SettingsManager::instance().setAiApiKey(d->aiKey->text());
        emitAiChanged();
    });
    connect(d->aiModel, &QLineEdit::editingFinished, this, [this, emitAiChanged]{
        SettingsManager::instance().setAiModel(d->aiModel->text());
        emitAiChanged();
    });
    connect(d->aiSystemPrompt, &QPlainTextEdit::textChanged, this, [this, emitAiChanged]{
        SettingsManager::instance().setAiSystemPrompt(d->aiSystemPrompt->toPlainText());
        emitAiChanged();
    });
    connect(d->aiStream, &EraSwitch::toggled, this, [this, emitAiChanged](bool on){
        SettingsManager::instance().setAiStreamEnabled(on);
        emitAiChanged();
    });

    connect(d->ttsBaseUrl, &QLineEdit::editingFinished, this, [this, emitAiChanged]{
        SettingsManager::instance().setTtsBaseUrl(d->ttsBaseUrl->text());
        emitAiChanged();
    });
    connect(d->ttsKey, &QLineEdit::editingFinished, this, [this, emitAiChanged]{
        SettingsManager::instance().setTtsApiKey(d->ttsKey->text());
        emitAiChanged();
    });
    connect(d->ttsModel, &QLineEdit::editingFinished, this, [this, emitAiChanged]{
        SettingsManager::instance().setTtsModel(d->ttsModel->text());
        emitAiChanged();
    });
    connect(d->ttsVoice, &QLineEdit::editingFinished, this, [this, emitAiChanged]{
        SettingsManager::instance().setTtsVoice(d->ttsVoice->text());
        emitAiChanged();
    });

    // 主题完全交给系统与 Qt 平台插件处理，不做任何手动 apply
}

SettingsWindow::~SettingsWindow() = default;

bool SettingsWindow::event(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange)
    {
        setWindowTitle(tr("设置"));
        if (d)
        {
            if (d->chooseBtn) d->chooseBtn->setText(tr("选择路径"));
            if (d->openBtn) d->openBtn->setText(tr("打开路径"));
            if (d->resetDirBtn) d->resetDirBtn->setText(tr("恢复默认"));
            if (d->resetBtn) d->resetBtn->setText(tr("还原初始状态"));
            if (d->openModelDirBtn) d->openModelDirBtn->setText(tr("打开当前模型路径"));
            if (d->modelNameTitle) d->modelNameTitle->setText(tr("模型名称："));
            if (d->watermarkTitle) d->watermarkTitle->setText(tr("去除水印："));
            if (d->wmChooseBtn) d->wmChooseBtn->setText(tr("选择文件"));
            if (d->wmClearBtn) d->wmClearBtn->setText(tr("取消所选"));
            if (d->chkBreath) d->chkBreath->setText(tr("自动呼吸"));
            if (d->chkBlink) d->chkBlink->setText(tr("自动眨眼"));
            if (d->chkGaze) d->chkGaze->setText(tr("视线跟踪"));
            if (d->chkPhysics) d->chkPhysics->setText(tr("物理模拟"));
            if (d->aiSystemPrompt) d->aiSystemPrompt->setPlaceholderText(tr("支持变量：$name$（当前模型名）"));
            if (d->aiStream) d->aiStream->setText(tr("是否流式输出"));
            if (d->clearCacheBtn) d->clearCacheBtn->setText(tr("清除缓存"));
            if (d->clearChatsBtn) d->clearChatsBtn->setText(tr("清除所有对话历史"));

            if (d->themeCombo && d->themeCombo->count() > 0) {
                d->themeCombo->setItemText(0, tr("跟随系统"));
            }
            if (d->languageCombo) {
                const int idxSystem = d->languageCombo->findData(QStringLiteral("system"));
                if (idxSystem >= 0) d->languageCombo->setItemText(idxSystem, tr("跟随系统"));
                const int idxZh = d->languageCombo->findData(QStringLiteral("zh_CN"));
                if (idxZh >= 0) d->languageCombo->setItemText(idxZh, tr("简体中文"));
            }

            if (d->basicForm) {
                if (auto* label = qobject_cast<QLabel*>(d->basicForm->labelForField(d->pathRowWidget))) {
                    label->setText(tr("模型路径："));
                }
                if (auto* label = qobject_cast<QLabel*>(d->basicForm->labelForField(d->topRowWidget))) {
                    label->setText(tr("当前模型："));
                }
                if (auto* label = qobject_cast<QLabel*>(d->basicForm->labelForField(d->themeCombo))) {
                    label->setText(tr("当前主题："));
                }
                if (auto* label = qobject_cast<QLabel*>(d->basicForm->labelForField(d->languageCombo))) {
                    label->setText(tr("当前语言："));
                }
            }

            if (d->aiForm) {
                if (auto* label = qobject_cast<QLabel*>(d->aiForm->labelForField(d->aiBaseUrlRow))) {
                    label->setText(tr("对话API："));
                }
                if (auto* label = qobject_cast<QLabel*>(d->aiForm->labelForField(d->aiKeyRow))) {
                    label->setText(tr("对话KEY："));
                }
                if (auto* label = qobject_cast<QLabel*>(d->aiForm->labelForField(d->aiModelRow))) {
                    label->setText(tr("对话模型："));
                }
                if (auto* label = qobject_cast<QLabel*>(d->aiForm->labelForField(d->aiSystemPromptRow))) {
                    label->setText(tr("对话人设："));
                }
                if (auto* label = qobject_cast<QLabel*>(d->aiForm->labelForField(d->ttsBaseUrlRow))) {
                    label->setText(tr("语音API："));
                }
                if (auto* label = qobject_cast<QLabel*>(d->aiForm->labelForField(d->ttsKeyRow))) {
                    label->setText(tr("语音KEY："));
                }
                if (auto* label = qobject_cast<QLabel*>(d->aiForm->labelForField(d->ttsModelRow))) {
                    label->setText(tr("语音模型："));
                }
                if (auto* label = qobject_cast<QLabel*>(d->aiForm->labelForField(d->ttsVoiceRow))) {
                    label->setText(tr("语音音色："));
                }
            }

            if (d->advancedForm) {
                if (auto* label = qobject_cast<QLabel*>(d->advancedForm->labelForField(d->texCapCombo))) {
                    label->setText(tr("贴图上限："));
                }
                if (auto* label = qobject_cast<QLabel*>(d->advancedForm->labelForField(d->msaaCombo))) {
                    label->setText(tr("MSAA："));
                }
                if (auto* label = qobject_cast<QLabel*>(d->advancedForm->labelForField(d->screenCombo))) {
                    label->setText(tr("模型显示："));
                }
                if (auto* label = qobject_cast<QLabel*>(d->advancedForm->labelForField(d->audioOutCombo))) {
                    label->setText(tr("音频输出："));
                }
                if (auto* label = qobject_cast<QLabel*>(d->advancedForm->labelForField(d->cleanupRowWidget))) {
                    label->setText(tr("清理："));
                }
            }

            if (d->tabs) {
                d->tabs->setTabText(0, tr("基本设置"));
                if (d->tabs->count() > 1) d->tabs->setTabText(1, tr("模型设置"));
                if (d->tabs->count() > 2) d->tabs->setTabText(2, tr("AI设置"));
                if (d->tabs->count() > 3) d->tabs->setTabText(3, tr("高级设置"));
            }

            if (d->wmFileLabel && d->wmFileLabel->text() == QStringLiteral("无")) {
                d->wmFileLabel->setText(tr("无"));
            }
            if (d->curModelName && d->curModelName->text() == QStringLiteral("(无)")) {
                d->curModelName->setText(tr("(无)"));
            }

            // Tooltips: re-apply translated content at runtime.
            if (d->tipBreath) d->tipBreath->setToolTip(tr("让角色在静止时也会轻微起伏（身体角度/呼吸参数）。开启视线跟踪时，自动呼吸不再影响头部；关闭后恢复。关闭本项后参数会复位。"));
            if (d->tipBlink) d->tipBlink->setToolTip(tr("自动眨眼并保留更自然的间隔与瞬目效果。关闭后眼部相关参数复位。"));
            if (d->tipGaze) d->tipGaze->setToolTip(tr("眼球、头部与身体随鼠标方向轻微转动，远距离时幅度会衰减。默认关闭；开启后将屏蔽自动呼吸对头部的影响。关闭后恢复眼球微动策略。"));
            if (d->tipPhysics) d->tipPhysics->setToolTip(tr("根据模型 physics3.json 的配置驱动物理（如头发/衣物摆动）。关闭后复位受影响参数。"));

            if (d->tipAiBaseUrl) d->tipAiBaseUrl->setToolTip(tr("填写 OpenAI 兼容接口的 Base URL。\n例： https://example.com/v1 （只填到 /v1 即可，程序会自动补全为 /v1/chat/completions）\n\n对应 OpenAI 参数：请求地址（endpoint）。\n用于：Chat Completions（/chat/completions）。"));
            if (d->tipAiKey) d->tipAiKey->setToolTip(tr("API Key（可留空）。\n通常是 Bearer Token：Authorization: Bearer <api_key>。\n\n对应 OpenAI 参数：请求头 Authorization。\n留空时将不发送 Authorization 头（适用于本地/内网免鉴权服务）。"));
            if (d->tipAiModel) d->tipAiModel->setToolTip(tr("要使用的对话模型名称。\n例：gpt-4o-mini、qwen-plus、deepseek-chat 等（取决于你的服务端支持）。\n\n对应 OpenAI 参数：model。"));
            if (d->tipAiSystemPrompt) d->tipAiSystemPrompt->setToolTip(tr("System Prompt（系统提示词 / 人设），用于规定 AI 的角色、语气、规则与边界。\n\n对应 OpenAI 参数：messages[0].role = \"system\" 的 content。\n\n支持变量：\n  • $name$：会在发送前替换成“当前模型名称”。\n\n示例：\n你是 $name$，是一只桌宠。回答要简短、温柔，并尽量使用口语。"));
            if (d->tipAiStream) d->tipAiStream->setToolTip(tr("开启后，AI 回复会“边生成边显示”（更像实时打字）。\n关闭后，会等完整回复生成后一次性显示。\n\n对应 OpenAI 参数：stream（true/false）。\n提示：启用语音时，仍建议开启流式输出以获得更自然的对话节奏。"));
            if (d->tipTtsBaseUrl) d->tipTtsBaseUrl->setToolTip(tr("填写 OpenAI 兼容 TTS 接口的 Base URL。\n例： https://example.com/v1 （只填到 /v1 即可，程序会自动补全为 /v1/audio/speech）\n\n对应 OpenAI 参数：请求地址（endpoint）。\n用于：Text-to-Speech（/audio/speech）。\n\n留空表示禁用语音：只显示文字，不请求语音。"));
            if (d->tipTtsKey) d->tipTtsKey->setToolTip(tr("TTS 的 API Key（可留空）。\n对应 OpenAI 参数：请求头 Authorization。\n留空时将不发送 Authorization 头。"));
            if (d->tipTtsModel) d->tipTtsModel->setToolTip(tr("TTS 模型名称。\n例：tts-1、gpt-4o-mini-tts 等（取决于服务端支持）。\n\n对应 OpenAI 参数：model。"));
            if (d->tipTtsVoice) d->tipTtsVoice->setToolTip(tr("语音音色（voice）。\n不同服务端的可用值不同，常见如：alloy、verse、aria 等。\n\n对应 OpenAI 参数：voice。"));

            // Re-localize default labels in dynamic combos.
            const QString defaultSuffixZh = QStringLiteral("（默认）");
            const QString defaultSuffixEn = QStringLiteral(" (Default)");
            const QString suffix = tr("（默认）");

            auto relocalizeComboDefaults = [&](QComboBox* combo){
                if (!combo) return;
                for (int i = 0; i < combo->count(); ++i) {
                    QString text = combo->itemText(i);
                    if (i == 0) {
                        combo->setItemText(i, tr("系统默认（默认）"));
                        continue;
                    }
                    bool hasDefault = false;
                    QString base = text;
                    if (base.endsWith(defaultSuffixZh)) {
                        base.chop(defaultSuffixZh.size());
                        hasDefault = true;
                    } else if (base.endsWith(defaultSuffixEn)) {
                        base.chop(defaultSuffixEn.size());
                        hasDefault = true;
                    }
                    if (hasDefault) combo->setItemText(i, base + suffix);
                }
            };
            relocalizeComboDefaults(d->screenCombo);
            relocalizeComboDefaults(d->audioOutCombo);
        }
    }
    return QMainWindow::event(e);
}

static void addWatchDirIfExists(QFileSystemWatcher* fsw, const QString& path) {
    QDir d(path); if (d.exists()) fsw->addPath(path);
}

void SettingsWindow::refreshModelList() {
    auto models = SettingsManager::instance().scanModels();
    const QSignalBlocker blocker(d->modelCombo);
    d->modelCombo->clear();
    QString sel = SettingsManager::instance().selectedModelFolder();
    int cur = -1; int i=0;
    for (const auto& e : models) { d->modelCombo->addItem(e.folderName, e.jsonPath); if (e.folderName == sel) cur = i; ++i; }
    if (cur < 0 && !models.isEmpty()) {
        for (int j=0;j<models.size();++j) if (models[j].folderName.compare("Hiyori", Qt::CaseInsensitive)==0) { cur = j; break; }
        if (cur < 0) cur = 0; SettingsManager::instance().setSelectedModelFolder(models[cur].folderName);
    }
    if (cur >= 0) d->modelCombo->setCurrentIndex(cur);
    if (d->fsw) {
        QString root = SettingsManager::instance().modelsRoot();
        QStringList watched = d->fsw->directories();
        for (const QString& w : watched) d->fsw->removePath(w);
        addWatchDirIfExists(d->fsw, root);
    }
}
