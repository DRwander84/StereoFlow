#pragma once
#include "routing/StereoRouter.h"
#include "ui/Widgets.h"
#include <QVBoxLayout>
#include <QWidget>
#include <vector>

namespace sf {

class ApplicationsPage : public QWidget {
    Q_OBJECT
public:
    explicit ApplicationsPage(StereoRouter* router, QWidget* parent = nullptr);
protected:
    void showEvent(QShowEvent*) override;
    void paintEvent(QPaintEvent*) override;
private:
    void rebuild();
    void updateStates();
    StereoRouter* router_;
    QWidget* container_;
    QVBoxLayout* rows_;
    std::vector<AppRow*> rowRefs_;
};

} // namespace sf
