#pragma once
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QStringList>

namespace sf::theme {

// ---- palette (dark) ----
inline const QColor bg(11, 13, 19), surface(18, 21, 30), surface2(27, 32, 44), hover(36, 42, 57);
inline const QColor line(255, 255, 255, 18), text(232, 236, 244), muted(138, 147, 168), dim(88, 96, 116);
inline const QColor accentL(56, 189, 248), accentR(167, 139, 250);       // left ear: sky, right ear: violet
inline const QColor good(52, 211, 153), warn(251, 191, 36);

inline const QColor& accent(int channel) { return channel == 0 ? accentL : accentR; }

// ---- fonts: Inter if its .ttf is shipped in <exe dir>/fonts, else Segoe UI Variable / Segoe UI ----
inline QString& family() { static QString f = "Segoe UI"; return f; }

inline void initFonts() {
    const QDir dir(QCoreApplication::applicationDirPath() + "/fonts");
    for (const QString& f : dir.entryList({"*.ttf", "*.otf"}, QDir::Files)) QFontDatabase::addApplicationFont(dir.filePath(f));
    const QStringList have = QFontDatabase::families();
    for (const char* p : {"Inter", "Segoe UI Variable Text", "Segoe UI", "Arial"})
        if (have.contains(QString::fromLatin1(p))) { family() = QString::fromLatin1(p); break; }
    QApplication::setFont(QFont(family(), 9));
}

inline QFont font(int px, QFont::Weight w = QFont::Normal) {
    QFont f(family());
    f.setPixelSize(px);
    f.setWeight(w);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

inline QColor withAlpha(QColor c, int a) { c.setAlpha(a); return c; }

// ---- the StereoFlow logo: sky -> violet rounded square with two dark "ear" dots. One drawing routine for the title bar,
// the window / taskbar icon and (identical geometry) assets/icon/StereoFlow.svg + .ico. `r` is the square, any size.
inline void paintLogo(QPainter& p, const QRectF& r) {
    const qreal k = r.width() / 22.0;
    QLinearGradient g(r.topLeft(), r.bottomRight());
    g.setColorAt(0, accentL);
    g.setColorAt(1, accentR);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(r, 7 * k, 7 * k);
    p.setBrush(bg);
    p.drawEllipse(QPointF(r.center().x() - 4.6 * k, r.center().y()), 2.7 * k, 2.7 * k);
    p.drawEllipse(QPointF(r.center().x() + 4.6 * k, r.center().y()), 2.7 * k, 2.7 * k);
}

// Window / taskbar icon, rendered crisp for every size (needs a QGuiApplication).
inline QIcon logoIcon() {
    QIcon icon;
    for (int s : {16, 20, 24, 32, 40, 48, 64, 128, 256}) {
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        const qreal m = s * (8.0 / 256.0);
        paintLogo(p, QRectF(m, m, s - 2 * m, s - 2 * m));
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace sf::theme
