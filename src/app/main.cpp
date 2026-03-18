#include <QApplication>
#include <QMainWindow>
#include <QFileInfo>
#include <QScreen>
#include <QTimer>
#include <QSurfaceFormat>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QDesktopServices>
#include <QUrl>
#include <QLabel>
#include <QPixmap>
#include <QFont>
#include <QVBoxLayout>
#include <QDir>
#include <QMessageBox>
#include <algorithm>
#include "engine/Renderer.hpp"
#include "common/SettingsManager.hpp"
#include "ui/SettingsWindow.hpp"
#include "ui/AboutWindow.hpp"
#include "ai/ChatController.hpp"
#include "ui/ChatWindow.hpp"
#include "ui/theme/ThemeApi.hpp"
#include "common/Utils.hpp"
#include <QEvent>
#include <QWindow>
#include <QShortcut>
#include <QTranslator>
#include <QtGlobal>
#include <cstdlib>

static bool isLinuxWayland()
{
#if defined(Q_OS_LINUX)
    return QGuiApplication::platformName().startsWith(QStringLiteral("wayland"));
#else
    return false;
#endif
}

#if defined(Q_OS_MACOS)
static QIcon resolveMacTrayIcon(const QString& resRoot, const QIcon& fallbackIcon)
{
    const QString preferredIconPath =
        QDir(resRoot).filePath(QStringLiteral("icons/menubar-icon-dark.png"));
    const QString fallbackMenubarIconPath =
        QDir(resRoot).filePath(QStringLiteral("icons/menubar-icon.png"));

    QIcon trayIcon(preferredIconPath);
    if (trayIcon.isNull()) {
        trayIcon = QIcon(fallbackMenubarIconPath);
    }
    if (trayIcon.isNull()) {
        trayIcon = fallbackIcon;
    }

    // Mark the menu bar icon as a template/mask icon so macOS automatically
    // renders it with the correct contrast for light and dark menu bar backgrounds.
    if (!trayIcon.isNull()) {
        trayIcon.setIsMask(true);
    }

    return trayIcon;
}
#endif

static void cacheWindowGeometry(QWidget* w)
{
#if defined(Q_OS_LINUX)
    if (!w) return;
    w->setProperty("amaigirl_cached_geometry", w->geometry());
#else
    Q_UNUSED(w);
#endif
}

static void restoreWindowGeometry(QWidget* w)
{
#if defined(Q_OS_LINUX)
    if (!w) return;
    const QRect cached = w->property("amaigirl_cached_geometry").toRect();
    if (!cached.isValid()) return;
    w->setGeometry(cached);
#else
    Q_UNUSED(w);
#endif
}

// Helper to center/reset window
static void centerAndSize(QMainWindow& win) {
    QScreen* screen = QGuiApplication::primaryScreen();
    int screenH = screen ? screen->geometry().height() : 900;
    int initH = screenH * 2 / 5;
    // 初始宽先按一半，稍后由 Renderer 发出建议再调整
    int initW = initH / 2;
    QRect scr = screen ? screen->availableGeometry() : QRect(0,0,1280,800);
    int x = scr.x() + (scr.width() - initW)/2;
    int y = scr.y() + (scr.height() - initH)/2;
    win.resize(initW, initH);
    win.move(x, y);
}

// Bring a window to front once (try without flags first; fallback to temporary topmost without re-show)
static void bringToFrontOnce(QWidget* w) {
    if (!w) return;
    w->show();
    w->raise();
    w->activateWindow();
    if (auto *wh = w->windowHandle()) wh->requestActivate();

#if defined(Q_OS_LINUX)
    if (isLinuxWayland()) {
        return;
    }
#endif

    // If already active, don't touch flags to avoid flicker
    QTimer::singleShot(50, w, [w]{
        if (w->isActiveWindow()) return;
        const bool hadTop = w->windowFlags().testFlag(Qt::WindowStaysOnTopHint);
        if (!hadTop) w->setWindowFlag(Qt::WindowStaysOnTopHint, true);
        w->raise();
        w->activateWindow();
        if (auto *wh2 = w->windowHandle()) wh2->requestActivate();
        // Remove temporary flag after a short delay without calling show() again (to avoid flicker)
        QTimer::singleShot(250, w, [w, hadTop]{
            if (!hadTop) {
                w->setWindowFlag(Qt::WindowStaysOnTopHint, false);
                w->raise(); // keep it above siblings
            }
        });
    });
}

// Helper to center any top-level widget on the screen that contains its parent/itself
static void centerOnCurrentScreen(QWidget* w)
{
    if (!w) return;
    QWidget* ref = w->parentWidget() ? w->parentWidget() : w;
    QScreen* screen = QGuiApplication::screenAt(ref->geometry().center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect scr = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
    const QSize sz = w->size();
    const int x = scr.x() + (scr.width() - sz.width()) / 2;
    const int y = scr.y() + (scr.height() - sz.height()) / 2;
    w->move(x, y);
}

// Helper to find preferred screen by SettingsManager, fallback to primary
static QScreen* resolvePreferredScreen()
{
    const QString preferred = SettingsManager::instance().preferredScreenName().trimmed();
    if (!preferred.isEmpty())
    {
        const QList<QScreen*> screens = QGuiApplication::screens();
        for (QScreen* s : screens)
        {
            if (s && s->name() == preferred)
                return s;
        }
    }
    return QGuiApplication::primaryScreen();
}

static void moveWindowToScreenCenter(QMainWindow& win, QScreen* screen)
{
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect scr = screen ? screen->availableGeometry() : QRect(0,0,1280,800);
    const QSize sz = win.size();
    const int x = scr.x() + (scr.width() - sz.width())/2;
    const int y = scr.y() + (scr.height() - sz.height())/2;
    win.move(x, y);
}

// Create a stable-ish signature for a screen. Used only to detect changes.
static QString screenSignature(QScreen* s)
{
    if (!s) return {};
    const QRect a = s->availableGeometry();
    return QStringLiteral("%1|%2x%3@%4,%5|dpr=%6")
        .arg(s->name())
        .arg(a.width()).arg(a.height())
        .arg(a.x()).arg(a.y())
        .arg(QString::number(s->devicePixelRatio(), 'f', 2));
}

// Reset window geometry for a target screen (same intent as clicking "还原初始状态").
static void resetWindowForScreen(QMainWindow& win, QScreen* screen, Renderer* renderer)
{
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect scr = screen ? screen->availableGeometry() : QRect(0,0,1280,800);

    const int targetH = std::max(300, scr.height() * 2 / 5);
    const int targetW = renderer ? renderer->suggestWidthForHeight(targetH) : (targetH/2);

    win.resize(targetW, targetH);
    const int x = scr.x() + (scr.width() - targetW)/2;
    const int y = scr.y() + (scr.height() - targetH)/2;
    win.move(x, y);

    SettingsManager::instance().setWindowGeometry(win.geometry());
    // Persist which display context produced this geometry
    SettingsManager::instance().setWindowGeometryScreen(screenSignature(screen));
}

// Return true if the window rect intersects any screen's available area by at least a few pixels.
static bool isRectVisibleOnAnyScreen(const QRect& r)
{
    if (!r.isValid()) return false;
    constexpr int kMinVisible = 16; // px
    const QList<QScreen*> screens = QGuiApplication::screens();
    for (QScreen* s : screens)
    {
        if (!s) continue;
        const QRect avail = s->availableGeometry();
        const QRect inter = r.intersected(avail);
        if (inter.width() >= kMinVisible && inter.height() >= kMinVisible)
            return true;
    }
    return false;
}

static bool loadAppTranslator(QApplication& app, QTranslator& translator, const QString& languageCode)
{
    app.removeTranslator(&translator);

    // Chinese is the current source language in this project.
    if (languageCode == QStringLiteral("zh_CN")) {
        return true;
    }

    const QString baseName = QStringLiteral("amaigirl_") + languageCode;
    const QString appResI18nDir = QDir(appResourceRootPath()).filePath(QStringLiteral("i18n"));

    bool loaded = translator.load(baseName, appResI18nDir);
    if (!loaded) {
        // fallback to searching Qt resource/system paths if present in future.
        loaded = translator.load(baseName);
    }
    if (loaded) {
        app.installTranslator(&translator);
    }
    return loaded;
}

int main(int argc, char *argv[]) {
#if defined(Q_OS_LINUX)
    const QByteArray qpaEnv = qgetenv("QT_QPA_PLATFORM");
    if (qpaEnv.isEmpty()) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("wayland;xcb"));
    }
#endif

    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(0);
    fmt.setStencilBufferSize(8);
    //fmt.setSamples(4); // MSAA x4: balance quality and memory (was 8)
    QSurfaceFormat::setDefaultFormat(fmt);

    // Work around a macOS/Qt shutdown crash in QApplication::~QApplication by
    // letting the process exit without running this destructor.
    auto* appPtr = new QApplication(argc, argv);
    QApplication& app = *appPtr;
#if defined(Q_OS_LINUX)
    if (!isLinuxWayland()) {
        QMessageBox::warning(
            nullptr,
            QStringLiteral("AmaiGirl"),
            QStringLiteral("AmaiGirl on Linux currently supports Wayland only.\n"
                           "Current Qt platform plugin: %1\n\n"
                           "The app will continue to run with current backend.")
                .arg(QGuiApplication::platformName()));
    }
#endif
    QCoreApplication::setApplicationName(QStringLiteral("AmaiGirl"));
    QApplication::setApplicationDisplayName(QStringLiteral("AmaiGirl"));
    QCoreApplication::setOrganizationName(QStringLiteral("IAIAYN"));
    QApplication::setQuitOnLastWindowClosed(false);

    // Bootstrap settings and models
    SettingsManager::instance().load();
    SettingsManager::instance().bootstrap(QCoreApplication::applicationDirPath());
    Theme::installApplicationStyle(app, SettingsManager::instance().theme());

    const QIcon appIcon(appResourcePath(QStringLiteral("icons/app-icon.png")));
    if (!appIcon.isNull()) {
        app.setWindowIcon(appIcon);
    }

    QTranslator appTranslator;
    loadAppTranslator(app, appTranslator, SettingsManager::instance().currentLanguage());

    QMainWindow win;
    win.setWindowFlag(Qt::FramelessWindowHint, true);
    win.setWindowFlag(Qt::WindowStaysOnTopHint, true);
    win.setWindowFlag(Qt::NoDropShadowWindowHint, true);
#if defined(Q_OS_WIN32)
    win.setWindowFlag(Qt::Tool, true);
#endif
    win.setAttribute(Qt::WA_TranslucentBackground, true);

    auto* renderer = new Renderer;
    // 始终指向当前有效的 Renderer；下方所有连接通过此指针间接调用，避免重建后悬垂指针
    Renderer** currentRenderer = &renderer;
    // 在窗口高度确定后，依据模型尺寸建议宽度
    QObject::connect(renderer, &Renderer::requestFitWidthForHeight, [&win](int h, int suggestedW){
        if (h <= 0 || suggestedW <= 0) return;
        // 保持高度，调整宽度
        QSize cur = win.size();
        if (std::abs(cur.width() - suggestedW) >= 2) {
            win.resize(suggestedW, h);
        }
    });
    renderer->setEnableBlink(SettingsManager::instance().enableBlink());
    renderer->setEnableBreath(SettingsManager::instance().enableBreath());
    renderer->setEnableGaze(SettingsManager::instance().enableGaze()); // 默认已在 SettingsManager 中改为 false
    renderer->setEnablePhysics(SettingsManager::instance().enablePhysics());
    // 注入 per-model 的去水印表达式
    renderer->setWatermarkExpression(SettingsManager::instance().watermarkExpPath());
    // 初始渲染选项：贴图上限与 MSAA
    renderer->setTextureCap(SettingsManager::instance().textureMaxDim());
    renderer->setMsaaSamples(SettingsManager::instance().msaaSamples());

    win.setCentralWidget(renderer);

    // Restore geometry if present
    if (SettingsManager::instance().hasWindowGeometry()) {
        QRect g = SettingsManager::instance().windowGeometry();
        int w_ = std::max(150, g.width());
        int h_ = std::max(300, g.height());
        win.resize(w_, h_);
        win.move(g.x(), g.y()); // allow negative and partially offscreen

        // If the last saved geometry was for a different display setup, reset like "还原初始状态".
        const QString savedSig = SettingsManager::instance().windowGeometryScreen();
        const QString nowSig = screenSignature(resolvePreferredScreen());
        if (!savedSig.isEmpty() && !nowSig.isEmpty() && savedSig != nowSig)
        {
            resetWindowForScreen(win, resolvePreferredScreen(), renderer);
        }
    } else {
        // Initial size: height = 2/5 of current screen height, width = height / 2
        QScreen* screen = QGuiApplication::primaryScreen();
        int screenH = screen ? screen->geometry().height() : 900; // fallback
        int initH = screenH * 2 / 5;
        int initW = initH / 2;
        win.setMinimumSize(150, 300);
        win.resize(initW, initH);

        SettingsManager::instance().setWindowGeometry(win.geometry());
        SettingsManager::instance().setWindowGeometryScreen(screenSignature(screen));
    }

    // Only relocate the window when it would be invisible due to screen changes.
    // If the display setup hasn't changed, we keep the persisted position as-is.
    if (!isRectVisibleOnAnyScreen(win.geometry()))
    {
        resetWindowForScreen(win, resolvePreferredScreen(), renderer);
    }

    win.show();
#if defined(Q_OS_LINUX)
    cacheWindowGeometry(&win);
    if (auto* wh = win.windowHandle()) {
        wh->setFlag(Qt::WindowStaysOnTopHint, true);
    }
#endif

    const QString resRoot = appResourceRootPath();

    // System tray (menu bar) icon and menu
    auto tray = new QSystemTrayIcon(&app);
#if defined(Q_OS_MACOS)
    tray->setIcon(resolveMacTrayIcon(resRoot, app.windowIcon()));
#else
    QIcon trayIcon(QDir(resRoot).filePath(QStringLiteral("icons/app-icon.png")));
    if (trayIcon.isNull()) {
        trayIcon = app.windowIcon();
    }
    tray->setIcon(trayIcon);
#endif
    tray->setToolTip(QStringLiteral("AmaiGirl"));

    auto menu = new QMenu();
    auto toggleAction = new QAction(QStringLiteral("隐藏"), menu);
    toggleAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H)); // Ctrl+H to toggle main window visibility
    auto chatAction = new QAction(QStringLiteral("聊天"), menu);
    chatAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T)); // Ctrl+T to toggle chat window visibility
    auto settingsAction = new QAction(QStringLiteral("设置"), menu);
    settingsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_S)); // Ctrl+S to open settings
    auto aboutAction = new QAction(QStringLiteral("关于"), menu);
    auto quitAction = new QAction(QStringLiteral("退出"), menu);

    auto applyMenuTexts = [&]() {
        if (win.isVisible()) toggleAction->setText(QObject::tr("隐藏"));
        else toggleAction->setText(QObject::tr("显示"));
        chatAction->setText(QObject::tr("聊天"));
        settingsAction->setText(QObject::tr("设置"));
        aboutAction->setText(QObject::tr("关于"));
        quitAction->setText(QObject::tr("退出"));
        tray->setToolTip(QStringLiteral("AmaiGirl"));
    };
    applyMenuTexts();

    // Settings window (default hidden)
    auto settingsWnd = new SettingsWindow(&win);

    // Chat window + controller
    auto chatWnd = new ChatWindow(&win);
    chatWnd->hide();
    auto chatCtl = new ChatController(&app);
    chatCtl->setChatWindow(chatWnd);
    chatCtl->setRenderer(renderer);
    chatCtl->applyPreferredAudioOutput();

    QObject::connect(settingsWnd, &SettingsWindow::preferredAudioOutputChanged, chatCtl, [chatCtl](const QString&){
        chatCtl->applyPreferredAudioOutput();
    });

    QObject::connect(settingsWnd, &SettingsWindow::preferredScreenChanged, &app, [&win, currentRenderer](const QString&){
        // Switching display should behave like "还原初始状态".
        resetWindowForScreen(win, resolvePreferredScreen(), *currentRenderer);
    });

    auto toggleChat = [chatWnd]{
        static bool s_firstShow = true;
        if (chatWnd->isVisible()) {
#if defined(Q_OS_LINUX)
            cacheWindowGeometry(chatWnd);
#endif
            chatWnd->hide();
        } else {
            if (s_firstShow) {
                // 首次显示时强制居中。之后不再改位置，避免每次 show() 被平台重新“自动摆放”导致漂移。
                centerOnCurrentScreen(chatWnd);
                s_firstShow = false;
            }
#if defined(Q_OS_LINUX)
            restoreWindowGeometry(chatWnd);
#endif
            bringToFrontOnce(chatWnd);
#if defined(Q_OS_LINUX)
            QTimer::singleShot(0, chatWnd, [chatWnd]{ restoreWindowGeometry(chatWnd); });
            cacheWindowGeometry(chatWnd);
#endif
        }
    };

    // Global shortcut for toggling chat window visibility (works even when main window is hidden)
    auto* chatShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_T), &win);
    chatShortcut->setContext(Qt::ApplicationShortcut);
    QObject::connect(chatShortcut, &QShortcut::activated, &app, [toggleChat]{ toggleChat(); });

    QObject::connect(chatAction, &QAction::triggered, &app, [toggleChat]{ toggleChat(); });

    QObject::connect(settingsWnd, &SettingsWindow::requestOpenChat, &app, [toggleChat]{ toggleChat(); });

    // --- Add shortcuts like "聊天" ---
    auto* toggleShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_H), &win);
    toggleShortcut->setContext(Qt::ApplicationShortcut);
    QObject::connect(toggleShortcut, &QShortcut::activated, &app, [winPtr=&win, toggleAction]{
        if (winPtr->isVisible()) {
#if defined(Q_OS_LINUX)
            cacheWindowGeometry(winPtr);
#endif
            winPtr->hide();
            toggleAction->setText(QStringLiteral("显示"));
        } else {
#if defined(Q_OS_LINUX)
            restoreWindowGeometry(winPtr);
#endif
            winPtr->show();
            winPtr->raise();
            winPtr->activateWindow();
#if defined(Q_OS_LINUX)
            if (auto* wh = winPtr->windowHandle()) {
                wh->setFlag(Qt::WindowStaysOnTopHint, true);
            }
            QTimer::singleShot(0, winPtr, [winPtr]{ restoreWindowGeometry(winPtr); });
            cacheWindowGeometry(winPtr);
#endif
            toggleAction->setText(QStringLiteral("隐藏"));
        }
    });

    auto* settingsShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_S), &win);
    settingsShortcut->setContext(Qt::ApplicationShortcut);
    QObject::connect(settingsShortcut, &QShortcut::activated, &app, [settingsWnd]{
        if (settingsWnd->isVisible()) {
#if defined(Q_OS_LINUX)
            cacheWindowGeometry(settingsWnd);
#endif
            settingsWnd->hide();
        } else {
#if defined(Q_OS_LINUX)
            restoreWindowGeometry(settingsWnd);
#endif
            bringToFrontOnce(settingsWnd);
#if defined(Q_OS_LINUX)
            QTimer::singleShot(0, settingsWnd, [settingsWnd]{ restoreWindowGeometry(settingsWnd); });
            cacheWindowGeometry(settingsWnd);
#endif
        }
    });

    QObject::connect(settingsWnd, &SettingsWindow::aiSettingsChanged, chatCtl, [chatCtl]{
        Q_UNUSED(chatCtl);
        // config is pulled on send; no-op placeholder.
    });

    QObject::connect(settingsWnd, &SettingsWindow::languageChanged, &app, [&app, &appTranslator, &applyMenuTexts](const QString& code){
        const bool loaded = loadAppTranslator(app, appTranslator, code);
        applyMenuTexts();
        if (!loaded && code != QStringLiteral("zh_CN")) {
            QMessageBox::information(nullptr,
                                     QObject::tr("提示"),
                                     QObject::tr("未找到对应语言包，已回退为内置中文文案。"));
        }
    });

    QObject::connect(settingsWnd, &SettingsWindow::themeChanged, &app, [&app](const QString& themeId){
        Theme::applyTheme(app, themeId);
    });

    QObject::connect(settingsWnd, &SettingsWindow::requestLoadModel, [currentRenderer, chatCtl](const QString& json){
        if (*currentRenderer) (*currentRenderer)->load(json);

        // derive model folder + dir from current settings
        const QString folder = SettingsManager::instance().selectedModelFolder();
        const QString modelDir = QDir(SettingsManager::instance().modelsRoot()).filePath(folder);
        chatCtl->onModelChanged(folder, modelDir);
        chatCtl->setRenderer(*currentRenderer);
    });

    QObject::connect(settingsWnd, &SettingsWindow::toggleBlink, &win, [currentRenderer](bool on){ if (*currentRenderer) (*currentRenderer)->setEnableBlink(on); });
    QObject::connect(settingsWnd, &SettingsWindow::toggleBreath, &win, [currentRenderer](bool on){ if (*currentRenderer) (*currentRenderer)->setEnableBreath(on); });
    QObject::connect(settingsWnd, &SettingsWindow::toggleGaze, &win, [currentRenderer](bool on){ if (*currentRenderer) (*currentRenderer)->setEnableGaze(on); });
    QObject::connect(settingsWnd, &SettingsWindow::togglePhysics, &win, [currentRenderer](bool on){ if (*currentRenderer) (*currentRenderer)->setEnablePhysics(on); });
    // watermark change
    QObject::connect(settingsWnd, &SettingsWindow::watermarkChanged, &win, [currentRenderer](const QString& p){ if (*currentRenderer) (*currentRenderer)->setWatermarkExpression(p); });
    QObject::connect(settingsWnd, &SettingsWindow::textureCapChanged, &win, [currentRenderer](int d){ if (*currentRenderer) (*currentRenderer)->setTextureCap(d); });

    // Disable MSAA rebuild logic for now (user rolled back earlier); keep signal connected harmlessly.
    QObject::connect(settingsWnd, &SettingsWindow::msaaChanged, &app, [currentRenderer](int samples){ if (*currentRenderer) (*currentRenderer)->setMsaaSamples(samples); Q_UNUSED(samples); });

    QObject::connect(settingsAction, &QAction::triggered, [settingsWnd]{
        if (settingsWnd->isVisible()) {
#if defined(Q_OS_LINUX)
            cacheWindowGeometry(settingsWnd);
#endif
            settingsWnd->hide();
        } else {
#if defined(Q_OS_LINUX)
            restoreWindowGeometry(settingsWnd);
#endif
            bringToFrontOnce(settingsWnd);
#if defined(Q_OS_LINUX)
            QTimer::singleShot(0, settingsWnd, [settingsWnd]{ restoreWindowGeometry(settingsWnd); });
            cacheWindowGeometry(settingsWnd);
#endif
        }
    });

    auto aboutWnd = new AboutWindow(&win);

    QObject::connect(aboutAction, &QAction::triggered, [aboutWnd]{
        if (aboutWnd->isVisible()) {
#if defined(Q_OS_LINUX)
            cacheWindowGeometry(aboutWnd);
#endif
            aboutWnd->hide();
        } else {
#if defined(Q_OS_LINUX)
            restoreWindowGeometry(aboutWnd);
#endif
            bringToFrontOnce(aboutWnd);
#if defined(Q_OS_LINUX)
            QTimer::singleShot(0, aboutWnd, [aboutWnd]{ restoreWindowGeometry(aboutWnd); });
            cacheWindowGeometry(aboutWnd);
#endif
        }
    });

    QObject::connect(toggleAction, &QAction::triggered, [winPtr=&win, toggleAction]{
        if (winPtr->isVisible()) {
#if defined(Q_OS_LINUX)
            cacheWindowGeometry(winPtr);
#endif
            winPtr->hide();
            toggleAction->setText(QObject::tr("显示"));
        } else {
#if defined(Q_OS_LINUX)
            restoreWindowGeometry(winPtr);
#endif
            winPtr->show();
            winPtr->raise();
            winPtr->activateWindow();
#if defined(Q_OS_LINUX)
            if (auto* wh = winPtr->windowHandle()) {
                wh->setFlag(Qt::WindowStaysOnTopHint, true);
            }
            QTimer::singleShot(0, winPtr, [winPtr]{ restoreWindowGeometry(winPtr); });
            cacheWindowGeometry(winPtr);
#endif
            toggleAction->setText(QObject::tr("隐藏"));
        }
    });

    QObject::connect(quitAction, &QAction::triggered, &app, &QCoreApplication::quit);

    menu->addAction(toggleAction);
    menu->addAction(chatAction);
    menu->addAction(settingsAction);
    menu->addAction(aboutAction);
    menu->addSeparator();
    menu->addAction(quitAction);
    tray->setContextMenu(menu);
    tray->show();

    // Event filter to persist geometry on move/resize
    class WinEventFilter : public QObject {
        bool eventFilter(QObject* obj, QEvent* ev) override {
            auto* w = qobject_cast<QMainWindow*>(obj);
            if (!w) return QObject::eventFilter(obj, ev);
            if (ev->type() == QEvent::Move || ev->type() == QEvent::Resize) {
                SettingsManager::instance().setWindowGeometry(w->geometry());
                // Update geometry display signature too (used for display-change detection).
                QScreen* s = QGuiApplication::screenAt(w->geometry().center());
                if (!s) s = resolvePreferredScreen();
                SettingsManager::instance().setWindowGeometryScreen(screenSignature(s));
            }
            return QObject::eventFilter(obj, ev);
        }
    };
    static WinEventFilter s_filter;
    win.installEventFilter(&s_filter);

    // Wire reset request
    QObject::connect(settingsWnd, &SettingsWindow::requestResetWindow, [winPtr=&win, currentRenderer]{
         QScreen* screen = QGuiApplication::screenAt(winPtr->geometry().center());
         if (!screen) screen = QGuiApplication::primaryScreen();
         resetWindowForScreen(*winPtr, screen, *currentRenderer);
     });

    // If no stored geometry, center to defaults
    if (!SettingsManager::instance().hasWindowGeometry()) {
        centerAndSize(win);
        SettingsManager::instance().setWindowGeometry(win.geometry());
    }

    // Choose initial model from settings (selected folder), fallback to any scanned model
    QString initialJson;
    auto entries = SettingsManager::instance().scanModels();
    QString folder = SettingsManager::instance().selectedModelFolder();
    for (const auto& e : entries) if (e.folderName == folder) { initialJson = e.jsonPath; break; }
    if (initialJson.isEmpty() && !entries.isEmpty()) {
        // Prefer Hiyori if available
        for (const auto& e : entries) {
            if (e.folderName.compare("Hiyori", Qt::CaseInsensitive) == 0) { initialJson = e.jsonPath; break; }
        }
        if (initialJson.isEmpty()) initialJson = entries.front().jsonPath;
    }

    if (!initialJson.isEmpty()) {
        const QString modelPath = initialJson;
        QTimer::singleShot(0, [currentRenderer, modelPath, chatCtl]{
            if (*currentRenderer) (*currentRenderer)->load(modelPath);
            const QString folder = SettingsManager::instance().selectedModelFolder();
            const QString modelDir = QDir(SettingsManager::instance().modelsRoot()).filePath(folder);
            chatCtl->onModelChanged(folder, modelDir);
        });
    }

    const int exitCode = app.exec();
    Q_UNUSED(appPtr);
    std::_Exit(exitCode);
}
