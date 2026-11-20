// Helpers shared by the screens' implementation files.
#pragma once

#include "pages.h"

namespace smalt {
namespace screens {

// The message for an empty library: loading, offline with nothing cached,
// login rejected, or genuinely empty.
QString emptyLibraryMessage(Shell *shell);

// A book row that is only built while it is on screen (long lists).
QWidget *lazyBookRow(Shell *shell, const BookItem &book, const QString &subtitle,
                     const QString &sortKey, QWidget *parent);

// A TextBlock as a plain widget pointer (style is TextBlock::Style).
QWidget *textBlock(Shell *shell, const QString &text, int style, QWidget *parent);

} // namespace screens
} // namespace smalt
