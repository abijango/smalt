// The Stats screens as pure builders: a snapshot (src/stats/snapshot.h) in, a
// widget out. The tab and the book section call these on open and after a
// sync; the screenshot tests call them with fixed snapshots.
//
// Spec: Stats ("Screens", "Rules"), Visual design, UX details ("E Ink rules").
// Task 23.
#pragma once

#include "smalt/ui_kit.h"
#include "stats/snapshot.h"

#include <QHash>
#include <QPixmap>
#include <QString>

class QWidget;

namespace smalt {
namespace ui {
namespace stats {

// The three screens under the chip switcher. Each is a column of widgets that
// is as tall as its content (the shell decides what to do about overflow).
QWidget *buildOverview(ComponentFactory *kit, const smalt::stats::StatsSnapshot &snapshot,
                       QWidget *parent);
QWidget *buildPatterns(ComponentFactory *kit, const smalt::stats::StatsSnapshot &snapshot,
                       QWidget *parent);
// `covers` holds thumbnails by BookOrbit book ID (missing ones draw the kit's
// placeholder). The highlight of the day is added only if it fits in
// `remainingPx` below the rest (0 = always).
QWidget *buildYear(ComponentFactory *kit, const smalt::stats::StatsSnapshot &snapshot,
                   const QHash<qint64, QPixmap> &covers, int remainingPx, QWidget *parent);

// "Your reading" for book detail: forecast box, day strip, tiles, sessions.
QWidget *buildYourReading(ComponentFactory *kit, const smalt::stats::BookSnapshot &snapshot,
                          QWidget *parent);

// Chip names, in order: the switcher and "remembers the last one used".
QStringList chipNames();

} // namespace stats
} // namespace ui
} // namespace smalt
