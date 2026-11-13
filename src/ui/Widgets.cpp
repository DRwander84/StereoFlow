#include "ui/Widgets.h"
#include "ui/Theme.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWindow>
#include <algorithm>
#include <cmath>

namespace sf {
using namespace theme;

static void setupPainter(QPainter& p) {
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
}

// ============================================================ TitleBar
TitleBar::TitleBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(46);
    setMouseTracking(true);
}
QRect TitleBar::minRect() const { return QRect(width() - 92, 0, 46, height()); }
QRect TitleBar::closeRect() const { return QRect(width() - 46, 0, 46, height()); }

void TitleBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    setupPainter(p);
    const QRectF logo(18, 12, 22, 22);
    paintLogo(p, logo);
    p.setPen(text);
    p.setFont(theme::font(14, QFont::DemiBold));
    p.drawText(QRect(50, 0, 200, height()), Qt::AlignVCenter | Qt::AlignLeft, "StereoFlow");

    if (hover_ == 0) { p.setPen(Qt::NoPen); p.setBrush(withAlpha(Qt::white, 16)); p.drawRect(minRect()); }
    if (hover_ == 1) { p.setPen(Qt::NoPen); p.setBrush(QColor(232, 64, 64)); p.drawRect(closeRect()); }
    p.setPen(QPen(hover_ == 0 ? text : muted, 1.4, Qt::SolidLine, Qt::RoundCap));
    const QPointF m = minRect().center();
    p.drawLine(QPointF(m.x() - 5, m.y()), QPointF(m.x() + 5, m.y()));
    p.setPen(QPen(hover_ == 1 ? Qt::white : muted, 1.4, Qt::SolidLine, Qt::RoundCap));
    const QPointF c = closeRect().center();
    p.drawLine(QPointF(c.x() - 4.5, c.y() - 4.5), QPointF(c.x() + 4.5, c.y() + 4.5));
    p.drawLine(QPointF(c.x() - 4.5, c.y() + 4.5), QPointF(c.x() + 4.5, c.y() - 4.5));
}
void TitleBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const QPoint pt = e->position().toPoint();
    if (minRect().contains(pt)) emit minimizeClicked();
    else if (closeRect().contains(pt)) emit closeClicked();
    else if (QWindow* w = window()->windowHandle()) w->startSystemMove();
}
void TitleBar::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pt = e->position().toPoint();
    const int h = minRect().contains(pt) ? 0 : closeRect().contains(pt) ? 1 : -1;
    if (h != hover_) { hover_ = h; update(); }
}
void TitleBar::leaveEvent(QEvent*) { hover_ = -1; update(); }

// ============================================================ SegTabs
SegTabs::SegTabs(const QStringList& labels, QWidget* parent) : QWidget(parent), labels_(labels) {
    setFixedHeight(48);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    anim_.setDuration(200);
    anim_.setEasingCurve(QEasingCurve::OutCubic);
    connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { pos_ = v.toReal(); update(); });
}
QRectF SegTabs::trackRect() const { return QRectF(16, 6, width() - 32, 36); }

void SegTabs::setCurrent(int index) {
    if (index == cur_ || index < 0 || index >= labels_.size()) return;
    anim_.stop();
    anim_.setStartValue(pos_);
    anim_.setEndValue(static_cast<qreal>(index));
    anim_.start();
    cur_ = index;
    emit currentChanged(index);
    update();
}
void SegTabs::paintEvent(QPaintEvent*) {
    QPainter p(this);
    setupPainter(p);
    const QRectF t = trackRect();
    p.setPen(Qt::NoPen);
    p.setBrush(surface);
    p.drawRoundedRect(t, 12, 12);
    const qreal w = t.width() / labels_.size();
    const QRectF ind(t.left() + pos_ * w + 3, t.top() + 3, w - 6, t.height() - 6);
    p.setPen(QPen(line, 1));
    p.setBrush(surface2);
    p.drawRoundedRect(ind, 9, 9);
    p.setPen(Qt::NoPen);
    QLinearGradient g(ind.left(), 0, ind.right(), 0);
    g.setColorAt(0, withAlpha(accentL, 200));
    g.setColorAt(1, withAlpha(accentR, 200));
    p.setBrush(g);
    p.drawRoundedRect(QRectF(ind.center().x() - 11, ind.bottom() - 3, 22, 2.5), 1.2, 1.2);
    for (int i = 0; i < labels_.size(); ++i) {
        const QRectF r(t.left() + i * w, t.top(), w, t.height());
        const bool on = (i == cur_);
        p.setPen(on ? text : (i == hover_ ? text : muted));
        p.setFont(theme::font(12, on ? QFont::DemiBold : QFont::Medium));
        p.drawText(r, Qt::AlignCenter, labels_[i]);
    }
}
void SegTabs::mousePressEvent(QMouseEvent* e) {
    const QRectF t = trackRect();
    const QPointF pt = e->position();
    if (!t.contains(pt)) return;
    setCurrent(std::clamp(static_cast<int>((pt.x() - t.left()) / (t.width() / labels_.size())), 0, int(labels_.size()) - 1));
}
void SegTabs::mouseMoveEvent(QMouseEvent* e) {
    const QRectF t = trackRect();
    const QPointF pt = e->position();
    const int h = t.contains(pt) ? std::clamp(static_cast<int>((pt.x() - t.left()) / (t.width() / labels_.size())), 0, int(labels_.size()) - 1) : -1;
    if (h != hover_) { hover_ = h; update(); }
}
void SegTabs::leaveEvent(QEvent*) { hover_ = -1; update(); }

// ============================================================ SwapButton
SwapButton::SwapButton(QWidget* parent) : QWidget(parent) {
    setFixedSize(52, 52);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setToolTip("Swap left and right (the sound moves too)");
    anim_.setDuration(320);
    anim_.setEasingCurve(QEasingCurve::InOutCubic);
    anim_.setStartValue(0.0);
    anim_.setEndValue(180.0);
    connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { angle_ = v.toReal(); update(); });
    connect(&anim_, &QVariantAnimation::finished, this, [this] { angle_ = 0; update(); });   // 180 degrees looks identical
}
void SwapButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    setupPainter(p);
    const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);
    QLinearGradient ring(r.topLeft(), r.bottomRight());
    ring.setColorAt(0, accentL);
    ring.setColorAt(1, accentR);
    p.setPen(QPen(QBrush(ring), hover_ ? 2.2 : 1.6));
    p.setBrush(down_ ? hover : surface2);
    p.drawEllipse(r);
    p.translate(r.center());
    p.rotate(angle_);
    p.setPen(QPen(hover_ ? Qt::white : text, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(QPointF(-10, -6), QPointF(10, -6));
    p.drawPolyline((QPolygonF() << QPointF(5, -11) << QPointF(10, -6) << QPointF(5, -1)));
    p.drawLine(QPointF(-10, 6), QPointF(10, 6));
    p.drawPolyline((QPolygonF() << QPointF(-5, 1) << QPointF(-10, 6) << QPointF(-5, 11)));
}
void SwapButton::mousePressEvent(QMouseEvent* e) { if (e->button() == Qt::LeftButton) { down_ = true; update(); } }
void SwapButton::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !down_) return;
    down_ = false;
    if (rect().contains(e->position().toPoint())) {
        anim_.stop();
        anim_.start();
        emit clicked();
    }
    update();
}
void SwapButton::mouseMoveEvent(QMouseEvent*) { if (!hover_) { hover_ = true; update(); } }
void SwapButton::leaveEvent(QEvent*) { hover_ = false; down_ = false; update(); }

// ============================================================ PillButton
PillButton::PillButton(const QString& t, QWidget* parent) : QWidget(parent), text_(t) {
    setFixedSize(QFontMetrics(theme::font(12, QFont::DemiBold)).horizontalAdvance(t) + 44, 32);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}
void PillButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    setupPainter(p);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(hover_ ? withAlpha(accentL, 160) : line, 1));
    p.setBrush(down_ ? hover : (hover_ ? surface2 : surface));
    p.drawRoundedRect(r, 16, 16);
    // refresh glyph
    const QPointF c(r.left() + 18, r.center().y());
    p.setPen(QPen(hover_ ? accentL : muted, 1.7, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(c.x() - 5.5, c.y() - 5.5, 11, 11), 40 * 16, 270 * 16);
    p.setBrush(hover_ ? accentL : muted);
    p.setPen(Qt::NoPen);
    p.drawPolygon((QPolygonF() << QPointF(c.x() + 5.5, c.y() - 6.5) << QPointF(c.x() + 7.5, c.y() - 1) << QPointF(c.x() + 2, c.y() - 1.5)));
    p.setPen(text);
    p.setFont(theme::font(12, QFont::DemiBold));
    p.drawText(QRectF(r.left() + 30, r.top(), r.width() - 34, r.height()), Qt::AlignVCenter | Qt::AlignLeft, text_);
}
void PillButton::mousePressEvent(QMouseEvent* e) { if (e->button() == Qt::LeftButton) { down_ = true; update(); } }
void PillButton::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !down_) return;
    down_ = false;
    update();
    if (rect().contains(e->position().toPoint())) emit clicked();
}
void PillButton::mouseMoveEvent(QMouseEvent*) { if (!hover_) { hover_ = true; update(); } }
void PillButton::leaveEvent(QEvent*) { hover_ = false; down_ = false; update(); }

// ============================================================ MeterStrip
MeterStrip::MeterStrip(int channel, QWidget* parent) : QWidget(parent), channel_(channel) { setFixedHeight(6); }

void MeterStrip::setLevel(float a) {
    const float db = a > 1e-4f ? 20.0f * std::log10(a) : -80.0f;
    const float v = std::clamp((db + 54.0f) / 54.0f, 0.0f, 1.0f);
    const float next = v > shown_ ? v : shown_ * 0.86f + v * 0.14f;     // fast attack, slow release
    if (std::fabs(next - shown_) > 0.004f || (next < 0.004f && shown_ > 0.0f)) {
        shown_ = next < 0.004f ? 0.0f : next;
        update();
    }
}
void MeterStrip::paintEvent(QPaintEvent*) {
    QPainter p(this);
    setupPainter(p);
    const QRectF r = rect();
    p.setPen(Qt::NoPen);
    p.setBrush(surface2);
    p.drawRoundedRect(r, 3, 3);
    if (shown_ <= 0.0f) return;
    QLinearGradient g(r.left(), 0, r.right(), 0);
    g.setColorAt(0, accent(channel_).darker(170));
    g.setColorAt(1, accent(channel_));
    p.setBrush(g);
    p.drawRoundedRect(QRectF(r.left(), r.top(), std::max<qreal>(6.0, r.width() * shown_), r.height()), 3, 3);
}

// ============================================================ EarSlider
EarSlider::EarSlider(int channel, QWidget* parent) : QSlider(Qt::Horizontal, parent), channel_(channel) {
    setRange(0, 100);
    setFixedHeight(40);
    setCursor(Qt::PointingHandCursor);
}
void EarSlider::paintEvent(QPaintEvent*) {
    QPainter p(this);
    setupPainter(p);
    const QColor ac = accent(channel_);
    const qreal left = 14, w = width() - 28, cy = height() / 2.0;
    const qreal t = (value() - minimum()) / qreal(std::max(1, maximum() - minimum()));
    const qreal x = left + t * w;
    p.setPen(Qt::NoPen);
    p.setBrush(surface2);
    p.drawRoundedRect(QRectF(left, cy - 4, w, 8), 4, 4);
    QLinearGradient g(left, 0, left + w, 0);
    g.setColorAt(0, ac.darker(190));
    g.setColorAt(1, ac);
    p.setBrush(g);
    p.drawRoundedRect(QRectF(left, cy - 4, std::max<qreal>(8.0, x - left), 8), 4, 4);
    p.setBrush(withAlpha(ac, hasFocus() || isSliderDown() ? 90 : 55));
    p.drawEllipse(QPointF(x, cy), 16, 16);
    p.setBrush(Qt::white);
    p.drawEllipse(QPointF(x, cy), 10, 10);
    p.setBrush(ac);
    p.drawEllipse(QPointF(x, cy), 4, 4);
}
void EarSlider::setFromX(qreal x) {
    const qreal t = std::clamp((x - 14) / std::max<qreal>(1, width() - 28), 0.0, 1.0);
    setValue(minimum() + qRound(t * (maximum() - minimum())));
}
void EarSlider::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    setSliderDown(true);
    setFromX(e->position().x());
    update();
}
void EarSlider::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) setFromX(e->position().x());
}
void EarSlider::mouseReleaseEvent(QMouseEvent*) { setSliderDown(false); update(); }

// ============================================================ AppRow
AppRow::AppRow(const WindowInfo& w, QWidget* parent) : QWidget(parent), key_(w.key), name_(w.appName), title_(w.title) {
    setFixedHeight(54);
    setMouseTracking(true);
    setToolTip(w.title);
    icon_ = appIcon(w.exePath).pixmap(28, 28);
}
QRectF AppRow::pillRect(int channel) const {
    const qreal pw = 56, ph = 30, gap = 6;
    const qreal right = width() - 10;
    const qreal x = channel == 1 ? right - pw : right - 2 * pw - gap;
    return QRectF(x, (height() - ph) / 2.0, pw, ph);
}
int AppRow::hitPill(const QPointF& pt) const {
    // A pill of a full ear is dead (no hover, no click) unless this very app is the one on that ear: it can still be removed.
    for (int ch = 0; ch < 2; ++ch)
        if (pillRect(ch).contains(pt)) return (pillOn_[ch] || state_ == ch) ? ch : -1;
    return -1;
}
void AppRow::setState(int channel) { if (channel != state_) { state_ = channel; update(); } }
void AppRow::setPillEnabled(int channel, bool on) {
    if (channel < 0 || channel > 1 || pillOn_[channel] == on) return;
    pillOn_[channel] = on;
    if (!on && hover_ == channel) { hover_ = -2; unsetCursor(); }
    update();
}

void AppRow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    setupPainter(p);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(state_ >= 0 ? withAlpha(accent(state_), 90) : line, 1));
    p.setBrush(hover_ != -2 ? surface2 : surface);
    p.drawRoundedRect(r, 12, 12);
    p.drawPixmap(QRectF(14, (height() - 28) / 2.0, 28, 28), icon_, QRectF(icon_.rect()));
    const qreal tx = 54, tw = pillRect(0).left() - tx - 8;
    const QFontMetrics fn(theme::font(13, QFont::DemiBold)), ft(theme::font(11));
    p.setPen(text);
    p.setFont(theme::font(13, QFont::DemiBold));
    p.drawText(QRectF(tx, 8, tw, 20), Qt::AlignVCenter | Qt::AlignLeft, fn.elidedText(name_, Qt::ElideRight, int(tw)));
    p.setPen(dim);
    p.setFont(theme::font(11));
    p.drawText(QRectF(tx, 28, tw, 18), Qt::AlignVCenter | Qt::AlignLeft, ft.elidedText(title_, Qt::ElideRight, int(tw)));
    for (int ch = 0; ch < 2; ++ch) {
        const QRectF pr = pillRect(ch);
        const bool on = state_ == ch, hv = hover_ == ch && pillOn_[ch];
        const bool disabled = !pillOn_[ch] && !on;
        if (disabled) p.setOpacity(0.35);
        if (on) {
            QLinearGradient g(pr.topLeft(), pr.bottomRight());
            g.setColorAt(0, accent(ch).lighter(112));
            g.setColorAt(1, accent(ch).darker(115));
            p.setPen(Qt::NoPen);
            p.setBrush(g);
        } else {
            p.setPen(QPen(hv ? withAlpha(accent(ch), 170) : line, 1));
            p.setBrush(hv ? withAlpha(accent(ch), 26) : Qt::NoBrush);
        }
        p.drawRoundedRect(pr, 15, 15);
        p.setPen(on ? bg : (hv ? text : muted));
        p.setFont(theme::font(12, QFont::DemiBold));
        p.drawText(pr, Qt::AlignCenter, ch == 0 ? "Left" : "Right");
        if (disabled) p.setOpacity(1.0);
    }
}
void AppRow::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const int h = hitPill(e->position());
    if (h >= 0) emit chosen(h);
}
void AppRow::mouseMoveEvent(QMouseEvent* e) {
    const int h = hitPill(e->position());
    if (h != hover_) { hover_ = h; setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor); update(); }
}
void AppRow::leaveEvent(QEvent*) { hover_ = -2; update(); }

// ============================================================ AppTileDelegate
void AppTileDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
    p->save();
    setupPainter(*p);
    const bool running = idx.data(Qt::UserRole + 1).toBool();
    const int channel = idx.data(Qt::UserRole + 2).toInt();
    const bool selected = opt.state & QStyle::State_Selected, hovered = opt.state & QStyle::State_MouseOver;
    const QRectF r = QRectF(opt.rect).adjusted(4, 4, -4, -4);
    if (selected || hovered) {
        p->setPen(QPen(selected ? withAlpha(accent(channel), 150) : line, 1));
        p->setBrush(selected ? withAlpha(accent(channel), 34) : surface2);
        p->drawRoundedRect(r, 12, 12);
    }
    p->setOpacity(running ? 1.0 : 0.45);
    const QIcon icon = idx.data(Qt::DecorationRole).value<QIcon>();
    const QPixmap pm = icon.pixmap(36, 36);
    p->drawPixmap(QRectF(r.center().x() - 18, r.top() + 10, 36, 36), pm, QRectF(pm.rect()));
    p->setPen(text);
    p->setFont(theme::font(11, QFont::Medium));
    const QString name = idx.data(Qt::DisplayRole).toString();
    p->drawText(QRectF(r.left() + 4, r.top() + 52, r.width() - 8, 18), Qt::AlignCenter,
                QFontMetrics(theme::font(11, QFont::Medium)).elidedText(name, Qt::ElideRight, int(r.width()) - 8));
    p->setOpacity(1.0);
    if (running) {            // tiny "live" dot
        p->setPen(Qt::NoPen);
        p->setBrush(good);
        p->drawEllipse(QPointF(r.right() - 9, r.top() + 9), 3.2, 3.2);
    }
    p->restore();
}

} // namespace sf
