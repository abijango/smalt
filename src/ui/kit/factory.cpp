#include "factory.h"

namespace smalt {
namespace ui {
namespace kit {

KitFactory::KitFactory(DeviceModel model, RefreshRequester *refresh)
    : theme_(deviceProfile(model), refresh)
{
}

QWidget *KitFactory::headerBar(const QString &title, QWidget *parent)
{
    return new HeaderBar(theme_, title, parent);
}

QWidget *KitFactory::statusLine(const QString &text, QWidget *parent)
{
    return new StatusLine(theme_, text, parent);
}

QWidget *KitFactory::sectionHeader(const QString &title, int count, QWidget *parent)
{
    return new SectionHeader(theme_, title, count, parent);
}

QWidget *KitFactory::primaryButton(const QString &text, QWidget *parent)
{
    return new Button(theme_, Button::Primary, text, parent);
}

QWidget *KitFactory::accentButton(const QString &text, QWidget *parent)
{
    return new Button(theme_, Button::Accent, text, parent);
}

QWidget *KitFactory::outlineButton(const QString &text, QWidget *parent)
{
    return new Button(theme_, Button::Outline, text, parent);
}

QWidget *KitFactory::chipRow(const QStringList &chips, QWidget *parent)
{
    return new ChipRow(theme_, chips, parent);
}

QWidget *KitFactory::bottomTabs(const QStringList &tabs, int active, QWidget *parent)
{
    return new BottomTabs(theme_, tabs, active, parent);
}

QWidget *KitFactory::pager(int page, int pageCount, QWidget *parent)
{
    return new Pager(theme_, page, pageCount, parent);
}

QWidget *KitFactory::searchField(QWidget *parent)
{
    return new SearchField(theme_, parent);
}

QWidget *KitFactory::banner(const QString &text, const QString &action, QWidget *parent)
{
    return new Banner(theme_, text, action, parent);
}

QWidget *KitFactory::dialog(const QString &text, const QStringList &actions, QWidget *parent)
{
    return new Dialog(theme_, text, actions, parent);
}

QWidget *KitFactory::cover(const CoverSpec &spec, double widthMm, QWidget *parent)
{
    return new CoverView(theme_, spec, widthMm, parent);
}

QWidget *KitFactory::coverRow(const QList<CoverSpec> &covers, QWidget *parent)
{
    return new CoverRow(theme_, covers, parent);
}

QWidget *KitFactory::listRow(const ListRowSpec &spec, QWidget *parent)
{
    return new ListRow(theme_, spec, parent);
}

QWidget *KitFactory::downloadBadge(BadgeState state, const QString &text, QWidget *parent)
{
    return new DownloadBadge(theme_, state, text, parent);
}

QWidget *KitFactory::segmentedProgress(int total, int done, QWidget *parent)
{
    return new SegmentedProgress(theme_, total, done, parent);
}

QWidget *KitFactory::coverStack(const CoverSpec &front, QWidget *parent)
{
    return new CoverStack(theme_, front, parent);
}

QWidget *KitFactory::letterGrid(QWidget *parent)
{
    return new LetterGrid(theme_, parent);
}

} // namespace kit

std::unique_ptr<ComponentFactory> createComponentFactory(DeviceModel model,
                                                         RefreshRequester *refresh)
{
    return std::unique_ptr<ComponentFactory>(new kit::KitFactory(model, refresh));
}

} // namespace ui
} // namespace smalt
