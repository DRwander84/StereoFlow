#pragma once
#include "routing/StereoRouter.h"
#include "utils/AppSettings.h"
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QStackedWidget>
#include <QTimer>
#include <QWidget>

namespace sf {

// Small, fixed-size, frameless, always-on-top window with a custom title bar and three tabs.
class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow(StereoRouter* router, AppSettings* settings, QWidget* parent = nullptr);
    void showMessage(const QString& text);
protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;
    void closeEvent(QCloseEvent*) override;
    // Drop an .exe / shortcut anywhere on the window: left half = left ear, right half = right ear (any tab).
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dragLeaveEvent(QDragLeaveEvent*) override;
    void dropEvent(QDropEvent*) override;
private:
    StereoRouter* router_;
    AppSettings* settings_;
    QString message_;
    QTimer messageTimer_;
    QRect footerRect() const;
    static bool hasLocalFiles(const QMimeData* m);
    int dropSide_ = -1;
};

} // namespace sf
