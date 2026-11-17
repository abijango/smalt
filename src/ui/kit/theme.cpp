#include "theme.h"

#include <QImage>
#include <QWidget>

namespace smalt {
namespace ui {
namespace kit {

namespace {
QString &familyStorage()
{
    static QString family;
    return family;
}
} // namespace

void setFontFamily(const QString &family)
{
    familyStorage() = family;
}

QString fontFamily()
{
    return familyStorage();
}

Theme::Theme(const DeviceProfile &profile, RefreshRequester *refresh)
    : profile_(profile), palette_(paletteFor(profile.model)), refresh_(refresh)
{
}

QFont Theme::font(FontRole role) const
{
    QFont f;
    if (!familyStorage().isEmpty())
        f.setFamily(familyStorage());
    f.setStyleHint(QFont::SansSerif);
    // E Ink: no colour fringes, and no hinting differences between machines.
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    // QFont::NoSubpixelAntialias is not in the Kobo's Qt 5.2; the panel renders
    // greyscale text there anyway.
    f.setStyleStrategy(QFont::StyleStrategy(QFont::PreferAntialias | QFont::NoSubpixelAntialias));
#else
    f.setStyleStrategy(QFont::PreferAntialias);
#endif
    f.setHintingPreference(QFont::PreferNoHinting);
    switch (role) {
    case FontRole::Small:
        f.setPixelSize(px(metrics::kFontSmallMm));
        f.setWeight(metrics::kWeightBody);
        break;
    case FontRole::Title:
        f.setPixelSize(px(metrics::kFontTitleMm));
        f.setWeight(metrics::kWeightTitle);
        break;
    case FontRole::Body:
    default:
        f.setPixelSize(px(metrics::kFontBodyMm));
        f.setWeight(metrics::kWeightBody);
        break;
    }
    return f;
}

QPixmap Theme::coverForDevice(const QPixmap &cover) const
{
    if (cover.isNull() || palette_.colourCovers)
        return cover;
    QImage img = cover.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int g = qGray(line[x]);
            line[x] = qRgba(g, g, g, qAlpha(line[x]));
        }
    }
    return QPixmap::fromImage(img);
}

void Theme::requestPartial(QWidget *w) const
{
    if (refresh_)
        refresh_->requestRefresh(w, RefreshKind::Partial);
}

void Theme::requestFull(QWidget *w) const
{
    if (refresh_)
        refresh_->requestRefresh(w, RefreshKind::Full);
}

} // namespace kit
} // namespace ui
} // namespace smalt
