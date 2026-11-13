#include "ui/MainWindow.h"
#include "ui/ApplicationsPage.h"
#include "core/ShortcutResolver.h"
#include "ui/MixerPage.h"
#include "ui/SettingsPage.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QVBoxLayout>
#include <windows.h>
#include <dwmapi.h>

namespace sf {
using namespace theme;

static constexpr int kW = 440, kH = 560, kFooter = 34;

MainWindow::MainWindow(StereoRouter* router, AppSettings* settings, QWidget* parent)
    : QWidget(parent), router_(router), settings_(settings) {
    setWindowTitle("StereoFlow");
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowMinimizeButtonHint);
    setFixedSize(kW, kH);                       // same size always, cannot be resized
    setAcceptDrops(true);

    auto* title = new TitleBar;
    auto* tabs = new SegTabs({"Stereo Mixer", "Applications", "Audio Settings"});
    auto* stack = new QStackedWidget;
    stack->addWidget(new MixerPage(router_));
    stack->addWidget(new ApplicationsPage(router_));
    stack->addWidget(new SettingsPage(router_));

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(1, 1, 1, kFooter);    // 1 px border, painted footer below
    v->setSpacing(0);
    v->addWidget(title);
    v->addWidget(tabs);
    v->addWidget(stack, 1);

    connect(title, &TitleBar::minimizeClicked, this, &QWidget::showMinimized);
    connect(title, &TitleBar::closeClicked, this, &QWidget::close);
    connect(tabs, &SegTabs::currentChanged, stack, &QStackedWidget::setCurrentIndex);
    connect(router_, &StereoRouter::message, this, &MainWindow::showMessage);
    connect(router_, &StereoRouter::assignmentsChanged, this, [this] { update(footerRect()); });

    messageTimer_.setSingleShot(true);
    messageTimer_.setInterval(9000);
    connect(&messageTimer_, &QTimer::timeout, this, [this] { message_.clear(); update(footerRect()); });

    if (isProcessElevated())
        QTimer::singleShot(1500, this, [this] {
            showMessage("Running as administrator: Windows blocks drag & drop from Explorer. Start StereoFlow normally.");
        });

    const QVariant pos = settings_->windowPos();
    if (pos.isValid() && QGuiApplication::screenAt(pos.toPoint())) move(pos.toPoint());
}

bool MainWindow::hasLocalFiles(const QMimeData* m) {
    if (!m || !m->hasUrls()) return false;
    for (const QUrl& u : m->urls()) if (u.isLocalFile()) return true;
    return false;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (hasLocalFiles(e->mimeData())) e->acceptProposedAction(); else e->ignore();
}

void MainWindow::dragMoveEvent(QDragMoveEvent* e) {
    if (!hasLocalFiles(e->mimeData())) { e->ignore(); return; }
    e->acceptProposedAction();
    const int side = e->position().x() < width() / 2.0 ? 0 : 1;
    if (side != dropSide_) {
        dropSide_ = side;
        const bool full = router_->earFull(side == 0 ? Channel::Left : Channel::Right);
        showMessage(full ? (side == 0 ? "LEFT ear is full (5 apps max)" : "RIGHT ear is full (5 apps max)")
                         : (side == 0 ? "Drop to add to the LEFT ear" : "Drop to add to the RIGHT ear"));
    }
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent* e) {
    dropSide_ = -1;
    QWidget::dragLeaveEvent(e);
}

void MainWindow::dropEvent(QDropEvent* e) {
    if (!hasLocalFiles(e->mimeData())) { e->ignore(); return; }
    const Channel ch = e->position().x() < width() / 2.0 ? Channel::Left : Channel::Right;
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls()) if (u.isLocalFile()) paths << u.toLocalFile();
    dropSide_ = -1;
    e->setDropAction(Qt::CopyAction);
    e->accept();
    // Let Explorer's drag finish first; launching an app inside the drop callback would stall the source.
    QMetaObject::invokeMethod(this, [this, paths, ch] { router_->assignFiles(paths, ch); }, Qt::QueuedConnection);
}

QRect MainWindow::footerRect() const { return QRect(1, height() - kFooter, width() - 2, kFooter - 1); }

void MainWindow::showMessage(const QString& t) {
    message_ = t;
    messageTimer_.start();
    update(footerRect());
}

void MainWindow::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    const int round = 2;                         // DWMWCP_ROUND: rounded corners on Windows 11 (ignored on Windows 10)
    DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &round, sizeof(round));
    const COLORREF border = RGB(34, 40, 54);
    DwmSetWindowAttribute(hwnd, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof(border));
}

void MainWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    QLinearGradient g(0, 0, 0, height());
    g.setColorAt(0, QColor(15, 18, 27));
    g.setColorAt(1, bg);
    p.fillRect(rect(), g);
    p.setPen(QPen(QColor(255, 255, 255, 22), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));

    // footer: routing state on the left, latest message on the right
    const QRect f = footerRect();
    p.setPen(QPen(line, 1));
    p.drawLine(f.left() + 14, f.top(), f.right() - 14, f.top());
    const int n = router_->runningCount();
    const bool live = n > 0;
    p.setPen(Qt::NoPen);
    p.setBrush(live ? good : dim);
    p.drawEllipse(QPointF(f.left() + 22, f.center().y() + 1), 3.5, 3.5);
    p.setFont(theme::font(11, QFont::Medium));
    p.setPen(live ? text : muted);
    const QString state = live ? QString("Routing %1 app%2").arg(n).arg(n == 1 ? "" : "s") : "Idle";
    p.drawText(QRect(f.left() + 34, f.top(), 150, f.height()), Qt::AlignVCenter | Qt::AlignLeft, state);
    if (!message_.isEmpty()) {
        const QRect m(f.left() + 170, f.top(), f.width() - 184, f.height());
        p.setPen(warn);
        p.setFont(theme::font(10, QFont::Medium));
        p.drawText(m, Qt::AlignVCenter | Qt::AlignRight | Qt::TextWordWrap, message_);
    }
}

void MainWindow::closeEvent(QCloseEvent* e) {
    settings_->setWindowPos(pos());
    e->accept();      // last window closed => application quits, original sound is restored
}

} // namespace sf
