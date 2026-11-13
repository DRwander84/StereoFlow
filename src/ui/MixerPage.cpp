#include "ui/MixerPage.h"
#include "ui/Theme.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QUrl>
#include <QVBoxLayout>

namespace sf {
using namespace theme;

// ---------------------------------------------------------------- EarList
EarList::EarList(int channel, QWidget* parent) : QListWidget(parent), channel_(channel) {
    setFrameShape(QFrame::NoFrame);
    setStyleSheet("QListWidget{background:transparent;}"
                  "QScrollBar:vertical{background:transparent;width:6px;margin:2px;}"
                  "QScrollBar::handle:vertical{background:#2c3346;border-radius:3px;min-height:24px;}"
                  "QScrollBar::add-line,QScrollBar::sub-line{height:0;}");
    viewport()->setAutoFillBackground(false);
    setViewMode(QListView::IconMode);
    setMovement(QListView::Static);               // items are dragged to the other ear, not moved around
    setResizeMode(QListView::Adjust);
    setWrapping(true);
    setUniformItemSizes(true);
    setGridSize(QSize(84, 90));
    setIconSize(QSize(36, 36));
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
    setDropIndicatorShown(false);
    setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setItemDelegate(new AppTileDelegate(this));
    connect(this, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        emit removeRequested(it->data(Qt::UserRole).toString());
    });
}

bool EarList::acceptable(QDropEvent* e) const {
    if (full_) return false;
    if (auto* src = qobject_cast<EarList*>(e->source())) return src != this;
    if (!e->mimeData()->hasUrls()) return false;
    for (const QUrl& u : e->mimeData()->urls()) if (u.isLocalFile()) return true;
    return false;
}
void EarList::dragEnterEvent(QDragEnterEvent* e) {
    if (acceptable(e)) { dropHover_ = true; viewport()->update(); e->acceptProposedAction(); } else e->ignore();
}
void EarList::dragMoveEvent(QDragMoveEvent* e) {
    if (acceptable(e)) e->acceptProposedAction(); else e->ignore();
}
void EarList::dragLeaveEvent(QDragLeaveEvent* e) {
    dropHover_ = false;
    viewport()->update();
    e->accept();
}
void EarList::dropEvent(QDropEvent* e) {
    dropHover_ = false;
    viewport()->update();
    if (!acceptable(e)) { e->ignore(); return; }
    if (auto* src = qobject_cast<EarList*>(e->source())) {
        QStringList keys;
        for (auto* it : src->selectedItems()) keys << it->data(Qt::UserRole).toString();
        e->setDropAction(Qt::CopyAction);    // copy: the source must not delete its items, the router rebuilds both lists
        e->accept();
        QMetaObject::invokeMethod(this, [this, keys] { emit appsDropped(keys); }, Qt::QueuedConnection);
        return;
    }
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls()) if (u.isLocalFile()) paths << u.toLocalFile();
    e->setDropAction(Qt::CopyAction);
    e->accept();
    QMetaObject::invokeMethod(this, [this, paths] { emit filesDropped(paths); }, Qt::QueuedConnection);
}
void EarList::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Delete) {
        QStringList keys;
        for (auto* it : selectedItems()) keys << it->data(Qt::UserRole).toString();
        for (const auto& k : keys) emit removeRequested(k);
        return;
    }
    QListWidget::keyPressEvent(e);
}
void EarList::paintEvent(QPaintEvent* e) {
    QListWidget::paintEvent(e);
    QPainter p(viewport());
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    const QRectF r = QRectF(viewport()->rect()).adjusted(2, 2, -2, -2);
    if (dropHover_) {
        p.setPen(QPen(accent(channel_), 1.6, Qt::DashLine));
        p.setBrush(withAlpha(accent(channel_), 28));
        p.drawRoundedRect(r, 14, 14);
    }
    if (count() == 0) {
        p.setPen(dim);
        p.setFont(theme::font(12, QFont::Medium));
        p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, "Drop an app here\nor pick one in Applications");
    }
}

// ---------------------------------------------------------------- EarCard
EarCard::EarCard(int channel, QWidget* parent) : QWidget(parent), channel_(channel) {
    list_ = new EarList(channel);
    meter_ = new MeterStrip(channel);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(8, 40, 8, 12);
    v->setSpacing(8);
    v->addWidget(list_, 1);
    v->addWidget(meter_);
}
void EarCard::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0, withAlpha(accent(channel_), 22));
    g.setColorAt(0.35, surface);
    g.setColorAt(1, surface);
    p.setPen(QPen(line, 1));
    p.setBrush(g);
    p.drawRoundedRect(r, 16, 16);
    p.setPen(Qt::NoPen);
    p.setBrush(accent(channel_));
    p.drawEllipse(QPointF(20, 22), 4, 4);
    p.setPen(text);
    p.setFont(theme::font(11, QFont::Bold));
    QFont f = p.font();
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.4);
    p.setFont(f);
    p.drawText(QRectF(32, 10, 90, 24), Qt::AlignVCenter, channel_ == 0 ? "LEFT EAR" : "RIGHT EAR");
    p.setPen(muted);
    p.setFont(theme::font(11, QFont::Medium));
    p.drawText(QRectF(width() - 72, 10, 60, 24), Qt::AlignVCenter | Qt::AlignRight, QString("%1/%2").arg(count_).arg(kMaxAppsPerEar));
}

// ---------------------------------------------------------------- MixerPage
MixerPage::MixerPage(StereoRouter* router, QWidget* parent) : QWidget(parent), router_(router) {
    left_ = new EarCard(0);
    right_ = new EarCard(1);
    auto* swap = new SwapButton;
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(16, 8, 16, 8);
    h->setSpacing(8);
    h->addWidget(left_, 1);
    h->addWidget(swap, 0, Qt::AlignVCenter);
    h->addWidget(right_, 1);

    connect(swap, &SwapButton::clicked, router_, &StereoRouter::swapSides);
    for (auto* card : {left_, right_}) {
        const Channel ch = card == left_ ? Channel::Left : Channel::Right;
        connect(card->list(), &EarList::appsDropped, this, [this, ch](const QStringList& keys) {
            for (const auto& k : keys) router_->setChannel(k, ch);
        });
        connect(card->list(), &EarList::filesDropped, this, [this, ch](const QStringList& p) { dropFiles(p, ch); });
        connect(card->list(), &EarList::removeRequested, router_, &StereoRouter::unassign);
    }
    connect(router_, &StereoRouter::assignmentsChanged, this, &MixerPage::rebuild);

    // ~30 fps meters; the tick does nothing unless this page is visible
    meterTimer_.setInterval(33);
    connect(&meterTimer_, &QTimer::timeout, this, [this] {
        if (!isVisible()) return;
        left_->meter()->setLevel(router_->outputPeak(Channel::Left));
        right_->meter()->setLevel(router_->outputPeak(Channel::Right));
    });
    meterTimer_.start();
    rebuild();
}

void MixerPage::dropFiles(const QStringList& paths, Channel ch) {
    router_->assignFiles(paths, ch);
}

void MixerPage::fill(EarCard* card, Channel ch) {
    EarList* list = card->list();
    list->clear();
    int n = 0;
    for (const auto& a : router_->assignments()) {
        if (a.channel != ch) continue;
        auto* it = new QListWidgetItem(appIcon(a.exePath), a.name, list);
        it->setData(Qt::UserRole, a.key);
        it->setData(Qt::UserRole + 1, router_->isRunning(a.key));
        it->setData(Qt::UserRole + 2, ch == Channel::Left ? 0 : 1);
        it->setToolTip(router_->isRunning(a.key) ? a.name : a.name + " (not running)");
        ++n;
    }
    card->setCount(n);
    card->list()->setFull(n >= kMaxAppsPerEar);
}

void MixerPage::rebuild() {
    fill(left_, Channel::Left);
    fill(right_, Channel::Right);
}

} // namespace sf
