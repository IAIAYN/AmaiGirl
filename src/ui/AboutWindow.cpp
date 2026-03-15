#include "ui/AboutWindow.hpp"
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>
#include <QScreen>
#include <QFont>
#include <QCoreApplication>
#include <QEvent>
#include "app/Version.hpp"
#include "common/Utils.hpp"

AboutWindow::AboutWindow(QWidget *parent) : QMainWindow(parent) {
    // Use native macOS window (keep title bar and traffic-light buttons), but hide title text
    setWindowTitle("");
    setWindowFlag(Qt::Window); // ensure standard window
    setWindowFlag(Qt::CustomizeWindowHint, false);
    setWindowFlag(Qt::WindowMaximizeButtonHint, false); // disable full screen/maximize
    setMinimumSize(200, 160);

    // Fixed size relative to screen: height=1/5 of screen, width computed from content
    QScreen* s = QGuiApplication::primaryScreen();
    int screenH = s ? s->geometry().height() : 900;
    int h = std::max(280, screenH / 5);

    auto central = new QWidget(this);
    setCentralWidget(central);
    auto v = new QVBoxLayout(central);
    v->setContentsMargins(24,24,24,24);
    v->setSpacing(8);

    // App icon
    auto iconLabel = new QLabel(central);
    QPixmap pm(appResourcePath(QStringLiteral("icons/app-icon.png")));
    if (!pm.isNull()) {
        int side = 96;
        iconLabel->setPixmap(pm.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        iconLabel->setAlignment(Qt::AlignCenter);
    }

    auto name = new QLabel("AmaiGirl", central);
    name->setAlignment(Qt::AlignCenter);
    // Enlarge and bold the app name
    QFont nameFont = name->font();
    nameFont.setPointSizeF(nameFont.pointSizeF() * 1.2);
    nameFont.setBold(true);
    name->setFont(nameFont);

    // version from constant
    auto version = new QLabel(QString(tr("版本: %1")).arg(AMAI_GIRL_VERSION), central);
    version->setObjectName("versionLabel");
    version->setAlignment(Qt::AlignCenter);
    QFont small = version->font(); small.setPointSizeF(small.pointSizeF() * 0.9); small.setBold(false);
    version->setFont(small);

    auto copyright = new QLabel(
        "Copyright © 2026 <a href=\"https://github.com/IAIAYN\" style=\"text-decoration: none;\">IAIAYN</a>",
        central
    );
    copyright->setTextFormat(Qt::RichText);
    copyright->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    copyright->setOpenExternalLinks(true);
    copyright->setFocusPolicy(Qt::NoFocus);
#if defined(Q_OS_MACOS)
    copyright->setAttribute(Qt::WA_MacShowFocusRect, false);
#endif
    copyright->setAlignment(Qt::AlignCenter);
    copyright->setFont(small);

    auto license = new QLabel(
        tr("自有代码协议: <a href=\"https://www.apache.org/licenses/LICENSE-2.0\" style=\"text-decoration: none;\">Apache-2.0</a>"),
        central);
    license->setObjectName("licenseLabel");
    license->setTextFormat(Qt::RichText);
    license->setOpenExternalLinks(true);
    license->setAlignment(Qt::AlignCenter);
    license->setFont(small);
    license->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    license->setFocusPolicy(Qt::NoFocus);

    auto thirdParty = new QLabel(
        tr("第三方协议: Qt(<a href=\"https://www.gnu.org/licenses/lgpl-3.0.html#license-text\" style=\"text-decoration: none;\">LGPL-3.0</a>)，"
           "Live2D(<a href=\"https://www.live2d.com/eula/live2d-proprietary-software-license-agreement_cn.html\" style=\"text-decoration: none;\">专有软件协议</a>)"),
        central);
    thirdParty->setObjectName("thirdPartyLabel");
    thirdParty->setTextFormat(Qt::RichText);
    thirdParty->setOpenExternalLinks(true);
    thirdParty->setAlignment(Qt::AlignCenter);
    thirdParty->setFont(small);
    thirdParty->setWordWrap(false);
    thirdParty->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    thirdParty->setFocusPolicy(Qt::NoFocus);

    v->addStretch(1);
    v->addWidget(iconLabel);
    v->addSpacing(4);
    v->addWidget(name);
    v->addWidget(version);
    v->addWidget(copyright);
    v->addWidget(license);
    v->addWidget(thirdParty);
    v->addStretch(2);

    int contentWidth = 0;
    for (QLabel* label : {iconLabel, name, version, copyright, license, thirdParty}) {
        contentWidth = std::max(contentWidth, label->sizeHint().width());
    }

    // Center to screen on first show
    QRect scr = s ? s->availableGeometry() : QRect(0,0,1280,800);
    const QMargins margins = v->contentsMargins();
    int w = contentWidth + margins.left() + margins.right() + 8;
    const int minW = 400;
    int maxW = scr.width() - 40;
    if (maxW < minW) maxW = minW;
    if (w < minW) w = minW;
    if (w > maxW) w = maxW;

    setFixedSize(w, h);

    int x = scr.x() + (scr.width() - w) / 2;
    int y = scr.y() + (scr.height() - h) / 2;
    move(x, y);
}

void AboutWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        if (auto* version = findChild<QLabel*>("versionLabel")) {
            version->setText(QString(tr("版本: %1")).arg(AMAI_GIRL_VERSION));
        }
        if (auto* license = findChild<QLabel*>("licenseLabel")) {
            license->setText(tr("自有代码协议: <a href=\"https://www.apache.org/licenses/LICENSE-2.0\" style=\"text-decoration: none;\">Apache-2.0</a>"));
        }
        if (auto* thirdParty = findChild<QLabel*>("thirdPartyLabel")) {
            thirdParty->setText(tr("第三方协议: Qt(<a href=\"https://www.gnu.org/licenses/lgpl-3.0.html#license-text\" style=\"text-decoration: none;\">LGPL-3.0</a>)，"
                               "Live2D(<a href=\"https://www.live2d.com/eula/live2d-proprietary-software-license-agreement_cn.html\" style=\"text-decoration: none;\">专有软件协议</a>)"));
        }

        if (auto* layout = qobject_cast<QVBoxLayout*>(centralWidget()->layout())) {
            int contentWidth = 0;
            const auto labels = centralWidget()->findChildren<QLabel*>();
            for (QLabel* label : labels) {
                contentWidth = std::max(contentWidth, label->sizeHint().width());
            }

            QScreen* s = QGuiApplication::primaryScreen();
            QRect scr = s ? s->availableGeometry() : QRect(0,0,1280,800);
            const QMargins margins = layout->contentsMargins();
            int w = contentWidth + margins.left() + margins.right() + 8;
            const int minW = 400;
            int maxW = scr.width() - 40;
            if (maxW < minW) maxW = minW;
            if (w < minW) w = minW;
            if (w > maxW) w = maxW;
            setFixedWidth(w);
        }
    }

    QMainWindow::changeEvent(event);
}
