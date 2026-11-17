// Shared helpers for the UI kit tests: a pinned font, golden image comparison
// and a refresh recorder.
#pragma once

#include "kit/gallery.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QtTest>

namespace kittest {

using namespace smalt;
using namespace smalt::ui;

// Pins the bundled "Smalt Test Sans" so text renders the same on every machine.
inline bool pinTestFont()
{
    const QString dir = QStringLiteral(SMALT_KIT_TEST_DIR "/fonts/");
    const int a =
            QFontDatabase::addApplicationFont(dir + QStringLiteral("SmaltTestSans-Regular.ttf"));
    const int b = QFontDatabase::addApplicationFont(dir + QStringLiteral("SmaltTestSans-Bold.ttf"));
    if (a < 0 || b < 0)
        return false;
    kit::setFontFamily(QStringLiteral("Smalt Test Sans"));
    return true;
}

inline bool isGrey(QRgb c)
{
    return qRed(c) == qGreen(c) && qGreen(c) == qBlue(c);
}

// Antialiased edges of coloured shapes and text on a tint round to a few levels of
// colour; treat a spread of up to 4 levels as neutral on the colour palette.
inline bool isNearGrey(QRgb c)
{
    const int hi = qMax(qRed(c), qMax(qGreen(c), qBlue(c)));
    const int lo = qMin(qRed(c), qMin(qGreen(c), qBlue(c)));
    return hi - lo <= 4;
}

// A pixel is "teal family" if it is a blend of Teal, Teal tint or Teal dark with
// white or black: red is the lowest channel and green/blue are well above it.
inline bool isTealFamily(QRgb c)
{
    return !isNearGrey(c) && qRed(c) < qGreen(c) && qRed(c) < qBlue(c)
            && qAbs(qGreen(c) - qBlue(c)) <= 24;
}

inline int countPixels(const QImage &img, bool (*pred)(QRgb), const QRect &area = QRect())
{
    const QRect r = area.isNull() ? img.rect() : area.intersected(img.rect());
    int n = 0;
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x)
            if (pred(img.pixel(x, y)))
                ++n;
    return n;
}

inline bool isNotGrey(QRgb c)
{
    return !isGrey(c);
}

inline bool isNotNearGrey(QRgb c)
{
    return !isNearGrey(c);
}

inline bool isNotGreyOrTeal(QRgb c)
{
    return !isNearGrey(c) && !isTealFamily(c);
}

// Compares with tests/ui/kit/golden/<name>.png. With SMALT_UPDATE_GOLDENS=1 the
// golden is (re)written instead. Returns an empty string when it matches.
inline QString compareGolden(const QImage &actual, const QString &name)
{
    const QString path = QStringLiteral(SMALT_KIT_GOLDEN_DIR "/") + name + QStringLiteral(".png");
    const QImage rgb = actual.convertToFormat(QImage::Format_RGB32);
    if (qgetenv("SMALT_UPDATE_GOLDENS") == "1") {
        QDir().mkpath(QFileInfo(path).absolutePath());
        if (!rgb.save(path, "PNG", 0))
            return QStringLiteral("cannot write ") + path;
        return QString();
    }
    QImage golden(path);
    if (golden.isNull())
        return QStringLiteral("missing golden %1 (run the ui_kit_update_goldens target)").arg(path);
    golden = golden.convertToFormat(QImage::Format_RGB32);
    if (golden.size() != rgb.size())
        return QStringLiteral("%1: size %2x%3, golden %4x%5")
                .arg(name)
                .arg(rgb.width())
                .arg(rgb.height())
                .arg(golden.width())
                .arg(golden.height());
    // Tolerate antialiasing noise from other Qt/FreeType builds, not layout changes.
    qint64 different = 0;
    for (int y = 0; y < rgb.height(); ++y) {
        const QRgb *a = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        const QRgb *g = reinterpret_cast<const QRgb *>(golden.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x) {
            if (qAbs(qRed(a[x]) - qRed(g[x])) > 40 || qAbs(qGreen(a[x]) - qGreen(g[x])) > 40
                || qAbs(qBlue(a[x]) - qBlue(g[x])) > 40)
                ++different;
        }
    }
    const double fraction = double(different) / (double(rgb.width()) * rgb.height());
    if (fraction > 0.002) {
        const QString out =
                QDir::temp().filePath(name.split('/').last() + QStringLiteral("-actual.png"));
        rgb.save(out);
        return QStringLiteral("%1: %2% of pixels differ from the golden (actual saved to %3)")
                .arg(name)
                .arg(fraction * 100)
                .arg(out);
    }
    return QString();
}

class RefreshLog : public RefreshRequester
{
public:
    void requestRefresh(QWidget *widget, RefreshKind kind) override
    {
        widgets.append(widget);
        kinds.append(kind);
    }

    int count(RefreshKind kind) const
    {
        int n = 0;
        for (int i = 0; i < kinds.size(); ++i)
            if (kinds.at(i) == kind)
                ++n;
        return n;
    }

    void clear()
    {
        widgets.clear();
        kinds.clear();
    }

    QList<QWidget *> widgets;
    QList<RefreshKind> kinds;
};

inline const QList<DeviceModel> &allModels()
{
    static const QList<DeviceModel> models = QList<DeviceModel>()
            << DeviceModel::ClaraColour << DeviceModel::ClaraBW << DeviceModel::LibraColour;
    return models;
}

} // namespace kittest
