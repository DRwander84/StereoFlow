#pragma once
#include "routing/StereoRouter.h"
#include "ui/Widgets.h"
#include <QTimer>
#include <QWidget>

namespace sf {

// "Audio Settings": a volume slider per ear with a live level meter.
class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(StereoRouter* router, QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QWidget* earCard(int channel, MeterStrip** meter);
    StereoRouter* router_;
    MeterStrip *leftMeter_ = nullptr, *rightMeter_ = nullptr;
    QTimer timer_;
};

} // namespace sf
