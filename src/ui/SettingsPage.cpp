#include "ui/SettingsPage.h"
#include "ui/Theme.h"
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

namespace sf {
using namespace theme;

namespace {
class Card : public QWidget {
public:
    Card(int channel, QWidget* parent = nullptr) : QWidget(parent), channel_(channel) {}
    void setPercent(int p) { percent_ = p; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0, withAlpha(accent(channel_), 20));
        g.setColorAt(0.5, surface);
        g.setColorAt(1, surface);
        p.setPen(QPen(line, 1));
        p.setBrush(g);
        p.drawRoundedRect(r, 16, 16);
        p.setPen(Qt::NoPen);
        p.setBrush(accent(channel_));
        p.drawEllipse(QPointF(24, 26), 4, 4);
        QFont f = theme::font(11, QFont::Bold);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 1.4);
        p.setFont(f);
        p.setPen(text);
        p.drawText(QRectF(36, 12, 200, 28), Qt::AlignVCenter, channel_ == 0 ? "LEFT EAR VOLUME" : "RIGHT EAR VOLUME");
        p.setFont(theme::font(26, QFont::DemiBold));
        p.setPen(accent(channel_));
        p.drawText(QRectF(width() - 120, 8, 100, 36), Qt::AlignVCenter | Qt::AlignRight, QString("%1%").arg(percent_));
    }
private:
    int channel_, percent_ = 100;
};
}

SettingsPage::SettingsPage(StereoRouter* router, QWidget* parent) : QWidget(parent), router_(router) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(16, 52, 16, 12);
    v->setSpacing(12);
    v->addWidget(earCard(0, &leftMeter_));
    v->addWidget(earCard(1, &rightMeter_));
    auto* hint = new QLabel("Applies to every app routed to that ear.");
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet("color:#586074;");
    v->addWidget(hint);
    v->addStretch(1);

    timer_.setInterval(33);
    connect(&timer_, &QTimer::timeout, this, [this] {
        if (!isVisible()) return;
        leftMeter_->setLevel(router_->outputPeak(Channel::Left));
        rightMeter_->setLevel(router_->outputPeak(Channel::Right));
    });
    timer_.start();
}

void SettingsPage::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHints(QPainter::TextAntialiasing);
    p.setPen(text);
    p.setFont(theme::font(15, QFont::DemiBold));
    p.drawText(QRect(18, 10, 300, 32), Qt::AlignVCenter, "Audio Settings");
}

QWidget* SettingsPage::earCard(int channel, MeterStrip** meter) {
    auto* card = new Card(channel);
    card->setFixedHeight(128);
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(18, 52, 18, 16);
    v->setSpacing(10);
    auto* slider = new EarSlider(channel);
    const Channel ch = channel == 0 ? Channel::Left : Channel::Right;
    slider->setValue(router_->earVolume(ch));
    card->setPercent(slider->value());
    *meter = new MeterStrip(channel);
    v->addWidget(slider);
    v->addWidget(*meter);
    connect(slider, &QSlider::valueChanged, this, [this, ch, card](int pct) {
        card->setPercent(pct);
        router_->setEarVolume(ch, pct);
    });
    return card;
}

} // namespace sf
