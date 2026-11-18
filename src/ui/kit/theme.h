// Theme: everything a component needs to paint itself for one device.
//
// All design values the spec does not give (sizes in millimetres, font sizes,
// paddings, press states) live in `metrics` below and nowhere else, so the
// maintainer can tune them against docs/mockups/ in one place.
#pragma once

#include "smalt/ui_kit.h"

#include <QFont>
#include <QPixmap>
#include <QString>

class QWidget;

namespace smalt {
namespace ui {
namespace kit {

// --- Chosen values (NOT in docs/SPEC.md; see the task 13 report) ---
namespace metrics {
// Font sizes are em heights in millimetres (converted with the device ppi).
constexpr double kFontBodyMm = 4.0; // List titles, buttons, tabs
constexpr double kFontSmallMm = 3.3; // Subtitles, status line, counts, format labels
constexpr double kFontTitleMm = 5.4; // Header title, dialog text
constexpr int kWeightTitle = 60; // "Medium weight": between Normal (50) and DemiBold (63)
constexpr int kWeightBody = 50;

constexpr double kPageMarginMm = 5.0; // Left and right edge of every row
constexpr double kHeaderHeightMm = 13.0;
constexpr double kStatusLineHeightMm = 9.0; // Equal to the touch minimum: it is tappable
constexpr double kSectionHeaderHeightMm = 10.0;
constexpr double kButtonHeightMm = 11.0;
constexpr double kButtonPadMm = 6.0;
constexpr double kButtonRadiusMm = 1.2;
constexpr double kChipHeightMm = 9.0;
constexpr double kChipPadMm = 4.0;
constexpr double kChipGapMm = 2.5;
constexpr double kTabsHeightMm = 14.0;
constexpr double kPagerHeightMm = 12.0;
constexpr double kSearchHeightMm = 10.5;
constexpr double kBannerHeightMm = 12.0;
constexpr double kListRowHeightMm = 20.0;
constexpr double kListCoverWidthMm = 10.0; // Thumbnails are 2:3
constexpr double kListCoverHeightMm = 15.0;
constexpr double kBadgeSizeMm = 9.0;
constexpr double kCoverRatio = 1.5; // Height / width
constexpr double kCoverGapMm = 4.0;
constexpr double kCoverRowLabelMm = 9.0; // Title line under each cover in a cover row
constexpr double kProgressBarMm = 1.4; // On a cover and for the segmented bar
constexpr double kSegmentGapMm = 0.7;
constexpr double kSegmentBarHeightMm = 1.6;
constexpr double kTabUnderlineMm = 0.8; // Active tab underline
constexpr double kCoverStackStepMm = 1.5;
constexpr double kLetterCellMm = 10.0; // Jump-to-letter grid cells
constexpr int kLetterColumns = 6;
constexpr double kDialogMaxWidthMm = 90.0;
constexpr double kDialogPadMm = 6.0;
constexpr double kDialogGapMm = 4.0;
constexpr int kDimAlpha = 110; // 0..255, black over the page behind a dialog

// Pixel rules: these are device pixels, not millimetres, on purpose.
constexpr int kHeaderRulePx = 2; // Ink rule under the header
constexpr int kListRulePx = 1; // Rule-grey divider between rows
constexpr int kOutlinePx = 2; // Outline buttons, selected chips
constexpr int kDialogBorderPx = 2; // Spec: "2 px black border"
} // namespace metrics

// Font used by every component. The device uses Nickel's interface font; the
// desktop has none, so this is one setting. Empty means the generic sans
// serif. Tests set it to the bundled "Smalt Test Sans".
void setFontFamily(const QString &family);
QString fontFamily();

enum class FontRole { Body, Small, Title };

class Theme
{
public:
    Theme(const DeviceProfile &profile, RefreshRequester *refresh);

    const DeviceProfile &profile() const { return profile_; }

    const Palette &palette() const { return palette_; }

    int px(double mm) const { return mmToPx(mm, profile_.ppi); }

    QFont font(FontRole role) const;

    // Text colours: only ever Ink, Secondary or Paper, never an accent.
    QColor textColour() const { return palette_.ink; }

    QColor secondaryTextColour() const { return palette_.secondary; }

    QColor onFillTextColour() const { return palette_.paper; }

    // Covers are always colour on colour devices, greyscale on Clara BW.
    QPixmap coverForDevice(const QPixmap &cover) const;

    void requestPartial(QWidget *w) const;
    void requestFull(QWidget *w) const;

private:
    DeviceProfile profile_;
    Palette palette_;
    RefreshRequester *refresh_;
};

} // namespace kit
} // namespace ui
} // namespace smalt
