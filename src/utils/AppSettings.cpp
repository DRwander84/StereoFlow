#include "utils/AppSettings.h"
#include <algorithm>

namespace sf {

AppSettings::AppSettings() : s_(QSettings::IniFormat, QSettings::UserScope, "StereoFlow", "StereoFlow") {}

int AppSettings::earVolume(Channel c) const {
    return std::clamp(s_.value(c == Channel::Left ? "volume/left" : "volume/right", 100).toInt(), 0, 100);
}
void AppSettings::setEarVolume(Channel c, int percent) {
    s_.setValue(c == Channel::Left ? "volume/left" : "volume/right", std::clamp(percent, 0, 100));
}

bool AppSettings::suppressOriginal() const { return s_.value("audio/suppressOriginal", true).toBool(); }

QList<Assignment> AppSettings::assignments() const {
    QList<Assignment> out;
    const int n = s_.beginReadArray("assignments");
    for (int i = 0; i < n; ++i) {
        s_.setArrayIndex(i);
        Assignment a;
        a.key = s_.value("key").toString();
        a.name = s_.value("name").toString();
        a.exePath = s_.value("exePath").toString();
        a.channel = s_.value("channel").toString() == "Right" ? Channel::Right : Channel::Left;
        if (!a.key.isEmpty() && !a.exePath.isEmpty()) out.append(a);
    }
    s_.endArray();
    return out;
}

void AppSettings::setAssignments(const QList<Assignment>& list) {
    s_.remove("assignments");
    s_.beginWriteArray("assignments", list.size());
    for (int i = 0; i < list.size(); ++i) {
        s_.setArrayIndex(i);
        const auto& a = list[i];
        s_.setValue("key", a.key);
        s_.setValue("name", a.name);
        s_.setValue("exePath", a.exePath);
        s_.setValue("channel", channelName(a.channel));
    }
    s_.endArray();
}

QVariant AppSettings::windowPos() const { return s_.value("ui/pos"); }
void AppSettings::setWindowPos(const QPoint& p) { s_.setValue("ui/pos", p); }

} // namespace sf
