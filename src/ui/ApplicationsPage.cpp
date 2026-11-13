#include "ui/ApplicationsPage.h"
#include "ui/Theme.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>

namespace sf {
using namespace theme;

ApplicationsPage::ApplicationsPage(StereoRouter* router, QWidget* parent) : QWidget(parent), router_(router) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 10, 16, 8);
    root->setSpacing(10);

    auto* top = new QHBoxLayout;
    top->addStretch(1);                                  // the heading is painted in paintEvent
    auto* refresh = new PillButton("Refresh");
    top->addWidget(refresh);
    root->addLayout(top);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollArea,QScrollArea>QWidget>QWidget{background:transparent;}"
                          "QScrollBar:vertical{background:transparent;width:6px;margin:2px;}"
                          "QScrollBar::handle:vertical{background:#2c3346;border-radius:3px;min-height:24px;}"
                          "QScrollBar::add-line,QScrollBar::sub-line{height:0;}");
    container_ = new QWidget;
    rows_ = new QVBoxLayout(container_);
    rows_->setContentsMargins(0, 0, 6, 0);
    rows_->setSpacing(6);
    scroll->setWidget(container_);
    root->addWidget(scroll, 1);

    connect(refresh, &PillButton::clicked, router_, &StereoRouter::refreshWindows);
    connect(router_, &StereoRouter::windowsChanged, this, &ApplicationsPage::rebuild);
    connect(router_, &StereoRouter::assignmentsChanged, this, &ApplicationsPage::updateStates);
    rebuild();
}

void ApplicationsPage::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    router_->refreshWindows();                           // always show what is open right now
}

void ApplicationsPage::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHints(QPainter::TextAntialiasing);
    p.setPen(text);
    p.setFont(theme::font(15, QFont::DemiBold));
    p.drawText(QRect(18, 10, 240, 32), Qt::AlignVCenter, "Open applications");
}

void ApplicationsPage::rebuild() {
    setUpdatesEnabled(false);
    rowRefs_.clear();
    while (QLayoutItem* it = rows_->takeAt(0)) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    for (const auto& w : router_->windows()) {
        auto* row = new AppRow(w);
        connect(row, &AppRow::chosen, this, [this, w](int ch) {
            Channel cur;
            const Channel want = ch == 0 ? Channel::Left : Channel::Right;
            if (router_->assignedChannel(w.key, &cur) && cur == want) router_->unassign(w.key);   // same button again = deselect
            else router_->assign(w.key, w.appName, w.exePath, want);
        });
        rows_->addWidget(row);
        rowRefs_.push_back(row);
    }
    if (rowRefs_.empty()) {
        auto* empty = new QLabel("No open windows found.\nOpen an app and press Refresh.");
        empty->setAlignment(Qt::AlignCenter);
        empty->setStyleSheet("color:#586074;");
        rows_->addWidget(empty);
    }
    rows_->addStretch(1);
    updateStates();
    setUpdatesEnabled(true);
}

void ApplicationsPage::updateStates() {
    const bool full[2] = {router_->earFull(Channel::Left), router_->earFull(Channel::Right)};   // counted once, not per row
    for (AppRow* r : rowRefs_) {
        Channel ch;
        r->setState(router_->assignedChannel(r->key(), &ch) ? (ch == Channel::Left ? 0 : 1) : -1);
        r->setPillEnabled(0, !full[0]);
        r->setPillEnabled(1, !full[1]);
    }
}

} // namespace sf
