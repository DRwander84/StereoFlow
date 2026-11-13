#pragma once
#include "routing/StereoRouter.h"
#include "ui/Widgets.h"
#include <QListWidget>
#include <QTimer>
#include <QWidget>

namespace sf {

// App tiles of one ear. Accepts tiles dragged from the other ear and .exe / .lnk files dragged from Explorer.
class EarList : public QListWidget {
    Q_OBJECT
public:
    explicit EarList(int channel, QWidget* parent = nullptr);
    void setFull(bool full) { if (full != full_) { full_ = full; if (full_) { dropHover_ = false; } viewport()->update(); } }
    bool isFull() const { return full_; }
signals:
    void appsDropped(const QStringList& keys);      // tiles from the other ear
    void filesDropped(const QStringList& paths);    // files from Explorer / desktop / Start menu
    void removeRequested(const QString& key);
protected:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dragLeaveEvent(QDragLeaveEvent*) override;
    void dropEvent(QDropEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void paintEvent(QPaintEvent*) override;
private:
    bool acceptable(QDropEvent*) const;
    int channel_;
    bool dropHover_ = false;
    bool full_ = false;                  // ear holds the maximum number of apps: nothing can be dropped on it
};

// One ear: title, live count, drop zone and level meter.
class EarCard : public QWidget {
    Q_OBJECT
public:
    EarCard(int channel, QWidget* parent = nullptr);
    EarList* list() const { return list_; }
    MeterStrip* meter() const { return meter_; }
    void setCount(int n) { if (n != count_) { count_ = n; update(); } }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    int channel_, count_ = 0;
    EarList* list_;
    MeterStrip* meter_;
};

class MixerPage : public QWidget {
    Q_OBJECT
public:
    explicit MixerPage(StereoRouter* router, QWidget* parent = nullptr);
private:
    void rebuild();
    void fill(EarCard* card, Channel ch);
    void dropFiles(const QStringList& paths, Channel ch);
    StereoRouter* router_;
    EarCard *left_, *right_;
    QTimer meterTimer_;
};

} // namespace sf
