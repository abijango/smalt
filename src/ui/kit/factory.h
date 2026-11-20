// KitFactory: the ComponentFactory from include/smalt/ui_kit.h (the interface lists every kit component).
//
// Signals (connect with the string-based SIGNAL/SLOT syntax; the factory
// returns plain QWidget pointers):
//
//   headerBar        searchTapped()
//   statusLine       tapped()
//   buttons          tapped()
//   chipRow          chipToggled(int index, bool selected)
//   bottomTabs       tabTapped(int index)
//   pager            previousTapped(), nextTapped(), counterTapped()
//   letterGrid       letterTapped(const QString &letter)
//   searchField      tapped()
//   banner           actionTapped()
//   dialog           actionTapped(int index)
//   downloadBadge    tapped()
//   coverRow         coverTapped(int index)
//   listRow          tapped(), badgeTapped()
#pragma once

#include "components.h"

namespace smalt {
namespace ui {
namespace kit {

class KitFactory : public ComponentFactory
{
public:
    KitFactory(DeviceModel model, RefreshRequester *refresh);

    const DeviceProfile &profile() const override { return theme_.profile(); }

    const Palette &palette() const override { return theme_.palette(); }

    const Theme &theme() const { return theme_; }

    // ComponentFactory
    QWidget *headerBar(const QString &title, QWidget *parent) override;
    QWidget *statusLine(const QString &text, QWidget *parent) override;
    QWidget *sectionHeader(const QString &title, int count, QWidget *parent) override;
    QWidget *primaryButton(const QString &text, QWidget *parent) override;
    QWidget *accentButton(const QString &text, QWidget *parent) override;
    QWidget *outlineButton(const QString &text, QWidget *parent) override;
    QWidget *chipRow(const QStringList &chips, QWidget *parent) override;
    QWidget *bottomTabs(const QStringList &tabs, int active, QWidget *parent) override;
    QWidget *pager(int page, int pageCount, QWidget *parent) override;
    QWidget *searchField(QWidget *parent) override;
    QWidget *banner(const QString &text, const QString &action, QWidget *parent) override;
    QWidget *dialog(const QString &text, const QStringList &actions, QWidget *parent) override;

    QWidget *cover(const CoverSpec &spec, double widthMm, QWidget *parent) override;
    QWidget *coverRow(const QList<CoverSpec> &covers, QWidget *parent) override;
    QWidget *listRow(const ListRowSpec &spec, QWidget *parent) override;
    QWidget *downloadBadge(BadgeState state, const QString &text, QWidget *parent) override;
    QWidget *segmentedProgress(int total, int done, QWidget *parent) override;
    QWidget *coverStack(const CoverSpec &front, QWidget *parent) override;
    QWidget *letterGrid(QWidget *parent) override;

private:
    Theme theme_;
};

} // namespace kit
} // namespace ui
} // namespace smalt
