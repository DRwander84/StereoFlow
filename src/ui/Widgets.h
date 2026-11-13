#pragma once
#include "core/WindowEnumerator.h"
#include <QPixmap>
#include <QSlider>
#include <QStyledItemDelegate>
#include <QVariantAnimation>
#include <QWidget>

namespace sf {

// Custom-painted, dependency-free controls (QPainter only): small, crisp, no style sheets needed.

class TitleBar : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget* parent = nullptr);
signals:
    void minimizeClicked();
    void closeClicked();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
private:
    QRect minRect() const;
    QRect closeRect() const;
    int hover_ = -1;
};

// Segmented tab bar with a sliding indicator.
class SegTabs : public QWidget {
    Q_OBJECT
public:
    explicit SegTabs(const QStringList& labels, QWidget* parent = nullptr);
public slots:
    void setCurrent(int index);
signals:
    void currentChanged(int index);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
private:
    QRectF trackRect() const;
    QStringList labels_;
    int cur_ = 0, hover_ = -1;
    qreal pos_ = 0;
    QVariantAnimation anim_;
};

// Round button with two stacked arrows (right over left); spins 180 degrees when pressed.
class SwapButton : public QWidget {
    Q_OBJECT
public:
    explicit SwapButton(QWidget* parent = nullptr);
signals:
    void clicked();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
private:
    qreal angle_ = 0;
    bool hover_ = false, down_ = false;
    QVariantAnimation anim_;
};

class PillButton : public QWidget {
    Q_OBJECT
public:
    explicit PillButton(const QString& text, QWidget* parent = nullptr);
signals:
    void clicked();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
private:
    QString text_;
    bool hover_ = false, down_ = false;
};

// Thin level meter (amplitude 0..1 shown on a dB scale, smoothed).
class MeterStrip : public QWidget {
    Q_OBJECT
public:
    explicit MeterStrip(int channel, QWidget* parent = nullptr);
    void setLevel(float amplitude);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    int channel_;
    float shown_ = 0.0f;
};

class EarSlider : public QSlider {
    Q_OBJECT
public:
    explicit EarSlider(int channel, QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    void setFromX(qreal x);
    int channel_;
};

// One row of the Applications tab: icon, name, window title and Left / Right toggles.
class AppRow : public QWidget {
    Q_OBJECT
public:
    explicit AppRow(const WindowInfo& w, QWidget* parent = nullptr);
    QString key() const { return key_; }
    void setState(int channel);          // -1 = not routed, 0 = left, 1 = right
    void setPillEnabled(int channel, bool on);   // a full ear disables its button (an app already on it keeps its own)
signals:
    void chosen(int channel);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
private:
    QRectF pillRect(int channel) const;
    int hitPill(const QPointF& p) const;
    QString key_, name_, title_;
    QPixmap icon_;
    int state_ = -1, hover_ = -2;       // hover: -2 none, -1 row, 0/1 pill
    bool pillOn_[2] = {true, true};
};

// Paints the app tiles of the ear lists.
class AppTileDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter*, const QStyleOptionViewItem&, const QModelIndex&) const override;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return QSize(84, 90); }
};

} // namespace sf
