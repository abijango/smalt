// Sample pages that show every component with fixed, fake data. Used by the
// desktop preview app (tools/ui-preview) and by the screenshot tests, so what
// you see is what the goldens contain. Not used on the device.
#pragma once

#include "factory.h"

#include <QImage>
#include <QStringList>

namespace smalt {
namespace ui {
namespace kit {
namespace gallery {

// Names of the single-component samples, in display order.
QStringList sampleNames();

// A parentless page `width` pixels wide holding one component (or a few of one
// kind) at its true size. The caller owns it.
QWidget *buildSample(const QString &name, KitFactory &factory, int width);

// A full-screen Home-style page at the device's true resolution, showing
// the components together.
QWidget *buildHomeFrame(KitFactory &factory);

// A deterministic, colourful fake cover.
QPixmap fakeCover(int seed);

// Renders a widget at its own size onto white, as the panel would show it.
QImage renderToImage(QWidget *widget);

} // namespace gallery
} // namespace kit
} // namespace ui
} // namespace smalt
