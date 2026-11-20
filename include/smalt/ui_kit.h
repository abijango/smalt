// The UI kit: design tokens, palettes, millimetre layout and components.
//
// Spec: Visual design (all; the design is locked), UX details and copy ("E Ink
// rules"), Large-library UX. Mockups: docs/mockups/. Implemented by task 13 in
// src/ui/kit/; screens (task 22) build only from this.
#pragma once

#include "smalt/device.h"

#include <QColor>
#include <QList>
#include <QPixmap>
#include <QString>
#include <QStringList>

#include <memory>

class QWidget;

namespace smalt {
namespace ui {

// --- Tokens: exactly the spec's colour table ---
namespace token {
constexpr unsigned int Ink = 0x111111; // All text and icons, header rules, decision buttons
constexpr unsigned int Secondary = 0x555555; // Supporting text, inactive tabs
constexpr unsigned int Rule = 0xDDDDDD; // Dividers between list rows
constexpr unsigned int Paper = 0xFFFFFF; // Backgrounds
constexpr unsigned int Teal = 0x3E6F6A; // Download button, progress bars, active tab underline
constexpr unsigned int TealTint = 0xDCE8E5; // Background of small download badges
constexpr unsigned int TealDark = 0x2F5853; // Icon on the teal tint
constexpr unsigned int MonoTint = 0xE6E6E6; // Clara BW: replaces Teal tint
// Stats heatmap steps, paper to teal (SPEC, Stats "Rules"): days with 0, under
// 15, under 30, under 60, and 60 or more minutes.
constexpr unsigned int HeatmapColour[5] = { 0xECECEC, 0xC9DCD8, 0x93B6B0, 0x5E8C86, 0x3E6F6A };
constexpr unsigned int HeatmapMono[5] = { 0xECECEC, 0xC4C4C4, 0x8E8E8E, 0x5A5A5A, 0x111111 };
} // namespace token

// The palette a screen paints with. Teal means "getting and progress" only;
// text is never coloured.
struct Palette
{
    QColor ink;
    QColor secondary;
    QColor rule;
    QColor paper;
    QColor accent; // Teal, or Ink on Clara BW.
    QColor accentTint; // Teal tint, or light grey on Clara BW.
    QColor accentDark; // Teal dark, or Ink on Clara BW.
    QColor heatmap[5]; // Stats heatmap steps (token::HeatmapColour or HeatmapMono).
    bool colourCovers = true;
};

// Colour palette on Clara Colour and Libra Colour; the monochrome mapping
// ("Monochrome palette (Clara BW)") on Clara BW.
inline Palette paletteFor(DeviceModel model)
{
    Palette p;
    p.ink = QColor(QRgb(token::Ink));
    p.secondary = QColor(QRgb(token::Secondary));
    p.rule = QColor(QRgb(token::Rule));
    p.paper = QColor(QRgb(token::Paper));
    if (model == DeviceModel::ClaraBW) {
        p.accent = QColor(QRgb(token::Ink));
        p.accentTint = QColor(QRgb(token::MonoTint));
        p.accentDark = QColor(QRgb(token::Ink));
        for (int i = 0; i < 5; ++i)
            p.heatmap[i] = QColor(QRgb(token::HeatmapMono[i]));
        p.colourCovers = false;
    } else {
        p.accent = QColor(QRgb(token::Teal));
        p.accentTint = QColor(QRgb(token::TealTint));
        p.accentDark = QColor(QRgb(token::TealDark));
        for (int i = 0; i < 5; ++i)
            p.heatmap[i] = QColor(QRgb(token::HeatmapColour[i]));
    }
    return p;
}

// --- Layout in millimetres ("Devices and scaling") ---
constexpr double kMinTouchTargetMm = 9.0; // "Touch targets at least 9 mm tall"
constexpr double kMmPerInch = 25.4;

inline int mmToPx(double mm, int ppi)
{
    return int(mm * ppi / kMmPerInch + 0.5);
}

inline int mmToPx(double mm, const DeviceProfile &profile)
{
    return mmToPx(mm, profile.ppi);
}

// --- E Ink refresh ("E Ink rules") ---
// Components ask for refreshes through this; the Nickel side implements it
// (task 30), and the desktop preview uses a no-op.
enum class RefreshKind {
    Partial, // List scrolling, button presses.
    Full, // Screen changes.
};

class RefreshRequester
{
public:
    virtual ~RefreshRequester() = default;
    virtual void requestRefresh(QWidget *widget, RefreshKind kind) = 0;
};

// --- Component data ---

// A book cover in lists, rows and stacks.
struct CoverSpec
{
    QPixmap pixmap; // May be null: a placeholder is drawn.
    QString title;
    QString format; // "CBZ", "PDF" for comics and PDFs; empty for books.
    double progress = -1; // 0..1, or negative for no bar.
};

enum class BadgeState {
    Download, // Not on the Kobo: teal tint with a download arrow.
    OnDevice, // Downloaded: plain check, no colour.
    Percent, // Downloading: live value as black text ("42%").
    None,
};

struct ListRowSpec
{
    CoverSpec cover;
    QString title;
    QString subtitle;
    BadgeState badge = BadgeState::None;
    QString badgeText; // For BadgeState::Percent.
    bool unavailable = false; // Offline and not downloaded: drawn in Secondary.
};

// --- Components ---
// The factory builds every component the screens use, styled for one device
// profile and palette. No animations. Returned widgets are owned by `parent`.
class ComponentFactory
{
public:
    virtual ~ComponentFactory() = default;

    virtual const DeviceProfile &profile() const = 0;
    virtual const Palette &palette() const = 0;

    virtual QWidget *headerBar(const QString &title,
                               QWidget *parent) = 0; // With search one tap away.
    virtual QWidget *statusLine(const QString &text,
                                QWidget *parent) = 0; // "Synced 10:42 · 3 waiting"
    virtual QWidget *sectionHeader(const QString &title, int count, QWidget *parent) = 0;
    virtual QWidget *primaryButton(const QString &text,
                                   QWidget *parent) = 0; // Solid black: decisions.
    virtual QWidget *accentButton(const QString &text, QWidget *parent) = 0; // Teal: download only.
    virtual QWidget *outlineButton(const QString &text, QWidget *parent) = 0; // Secondary actions.
    virtual QWidget *chipRow(const QStringList &chips, QWidget *parent) = 0;
    virtual QWidget *bottomTabs(const QStringList &tabs, int active, QWidget *parent) = 0;
    virtual QWidget *pager(int page, int pageCount, QWidget *parent) = 0; // "Page 1 of 54"
    virtual QWidget *searchField(QWidget *parent) = 0;
    virtual QWidget *banner(const QString &text, const QString &action, QWidget *parent) = 0;
    // Dialog with a 2 px black border over a dimmed page, no shadow.
    virtual QWidget *dialog(const QString &text, const QStringList &actions, QWidget *parent) = 0;
    virtual QWidget *cover(const CoverSpec &spec, double widthMm, QWidget *parent) = 0;
    virtual QWidget *coverRow(const QList<CoverSpec> &covers, QWidget *parent) = 0;
    virtual QWidget *listRow(const ListRowSpec &spec, QWidget *parent) = 0;
    virtual QWidget *downloadBadge(BadgeState state, const QString &text, QWidget *parent) = 0;
    virtual QWidget *segmentedProgress(int total, int done, QWidget *parent) = 0; // Series bar.
    virtual QWidget *coverStack(const CoverSpec &front, QWidget *parent) = 0;
    virtual QWidget *letterGrid(QWidget *parent) = 0; // Jump-to-letter: A-Z and #.
};

// Defined by task 13 (src/ui/kit/). `refresh` may be null (no-op) and is not owned.
std::unique_ptr<ComponentFactory> createComponentFactory(DeviceModel model,
                                                         RefreshRequester *refresh);

} // namespace ui
} // namespace smalt
