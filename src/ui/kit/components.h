// UI kit components. All are plain QWidgets that paint themselves with
// QPainter (no QML, no SVG, no animations).
//
// Tap handling: every tappable component emits a signal from the table in
// `factory.h`. Screens can connect to them with the string-based
// connect(widget, SIGNAL(tapped()), ...) even though the factory returns plain
// QWidget pointers.
//
// E Ink: a press requests a partial refresh of the widget; actions that change
// screens (tab change, dialog shown) also request a full refresh.
#pragma once

#include "theme.h"

#include <QList>
#include <QPixmap>
#include <QRect>
#include <QStringList>
#include <QWidget>

namespace smalt {
namespace ui {
namespace kit {

// Shared base: theme copy, press tracking, partial refresh on press.
class KitWidget : public QWidget
{
    Q_OBJECT
public:
    KitWidget(const Theme &theme, QWidget *parent);

    const Theme &theme() const { return theme_; }

    // Height in millimetres, for the touch-target check.
    double heightMm() const { return height() * metrics_mmPerPx(); }

protected:
    // Which tappable region is at `pos`, or -1.
    virtual int regionAt(const QPoint &pos) const;
    virtual void activated(int region);

    int pressedRegion() const { return pressed_; }

    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

    void fixHeightMm(double mm);
    void paintPressed(QPainter &p, const QRect &r) const;

    Theme theme_;

private:
    double metrics_mmPerPx() const { return 25.4 / theme_.profile().ppi; }

    int pressed_ = -1;
};

class HeaderBar : public KitWidget
{
    Q_OBJECT
public:
    HeaderBar(const Theme &theme, const QString &title, QWidget *parent);
    void setTitle(const QString &title);
    QRect searchRect() const;
signals:
    void searchTapped();

protected:
    int regionAt(const QPoint &pos) const override;
    void activated(int region) override;
    void paintEvent(QPaintEvent *) override;

private:
    QString title_;
};

class StatusLine : public KitWidget
{
    Q_OBJECT
public:
    StatusLine(const Theme &theme, const QString &text, QWidget *parent);
    void setText(const QString &text);
signals:
    void tapped();

protected:
    int regionAt(const QPoint &pos) const override;
    void activated(int region) override;
    void paintEvent(QPaintEvent *) override;

private:
    QString text_;
};

class SectionHeader : public KitWidget
{
    Q_OBJECT
public:
    SectionHeader(const Theme &theme, const QString &title, int count, QWidget *parent);
    void setCount(int count);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString title_;
    int count_;
};

class Button : public KitWidget
{
    Q_OBJECT
public:
    enum Kind { Primary, Accent, Outline };

    Button(const Theme &theme, Kind kind, const QString &text, QWidget *parent);
    void setText(const QString &text);
    QSize sizeHint() const override;
signals:
    void tapped();

protected:
    int regionAt(const QPoint &pos) const override;
    void activated(int region) override;
    void paintEvent(QPaintEvent *) override;

private:
    Kind kind_;
    QString text_;
};

class ChipRow : public KitWidget
{
    Q_OBJECT
public:
    ChipRow(const Theme &theme, const QStringList &chips, QWidget *parent);
    void setSelected(const QList<int> &indexes);

    QList<int> selected() const { return selected_; }

    void clearSelection(); // "One tap clears them all"
    QList<QRect> chipRects() const;
signals:
    void chipToggled(int index, bool selected);

protected:
    int regionAt(const QPoint &pos) const override;
    void activated(int region) override;
    void paintEvent(QPaintEvent *) override;

private:
    QStringList chips_;
    QList<int> selected_;
};

class BottomTabs : public KitWidget
{
    Q_OBJECT
public:
    BottomTabs(const Theme &theme, const QStringList &tabs, int active, QWidget *parent);

    int active() const { return active_; }

    void setActive(int index);
    QRect tabRect(int index) const;
signals:
    void tabTapped(int index);

protected:
    int regionAt(const QPoint &pos) const override;
    void activated(int region) override;
    void paintEvent(QPaintEvent *) override;

private:
    QStringList tabs_;
    int active_;
};

class Pager : public KitWidget
{
    Q_OBJECT
public:
    Pager(const Theme &theme, int page, int pageCount, QWidget *parent);
    void setPage(int page, int pageCount);

    int page() const { return page_; }

    int pageCount() const { return pageCount_; }

signals:
    void previousTapped();
    void nextTapped();
    void counterTapped(); // Opens the jump-to-letter grid.

protected:
    int regionAt(const QPoint &pos) const override;
    void activated(int region) override;
    void paintEvent(QPaintEvent *) override;

private:
    QRect previousRect() const;
    QRect nextRect() const;
    int page_;
    int pageCount_;
};

// Jump-to-letter grid: A-Z and "#". Shown by the screen when the pager's
// counter is tapped.
class LetterGrid : public KitWidget
{
    Q_OBJECT
public:
    LetterGrid(const Theme &theme, QWidget *parent);
    static QString letters();
    QRect cellRect(int index) const;
signals:
    void letterTapped(const QString &letter);

protected:
    int regionAt(const QPoint &pos) const override;
    void activated(int region) override;
    void paintEvent(QPaintEvent *) override;
};

class SearchField : public KitWidget
{
    Q_OBJECT
public:
    SearchField(const Theme &theme, QWidget *parent);
    void setText(const QString &text);
signals:
    void tapped(); // The screen opens Nickel's keyboard.

protected:
    int regionAt(const QPoint &pos) const override;
    void activated(int region) override;
    void paintEvent(QPaintEvent *) override;

private:
    QString text_;
};

class Banner : public KitWidget
{
    Q_OBJECT
public:
    Banner(const Theme &theme, const QString &text, const QString &action, QWidget *parent);
    QRect actionRect() const;
signals:
    void actionTapped();

protected:
    int regionAt(const QPoint &pos) const override;
    void activated(int region) override;
    void paintEvent(QPaintEvent *) override;

private:
    QString text_;
    QString action_;
};

class DownloadBadge : public KitWidget
{
    Q_OBJECT
public:
    DownloadBadge(const Theme &theme, BadgeState state, const QString &text, QWidget *parent);
    void setState(BadgeState state, const QString &text);
signals:
    void tapped();

protected:
    int regionAt(const QPoint &pos) const override;
    void activated(int region) override;
    void paintEvent(QPaintEvent *) override;

private:
    BadgeState state_;
    QString text_;
};

class CoverView : public KitWidget
{
    Q_OBJECT
public:
    CoverView(const Theme &theme, const CoverSpec &spec, double widthMm, QWidget *parent);
    void setSpec(const CoverSpec &spec);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    CoverSpec spec_;
};

class CoverRow : public KitWidget
{
    Q_OBJECT
public:
    CoverRow(const Theme &theme, const QList<CoverSpec> &covers, QWidget *parent);
    QRect coverRect(int index) const;
    static constexpr double kCoverWidthMm = 21.0; // Same physical size on every device
signals:
    void coverTapped(int index);

protected:
    int regionAt(const QPoint &pos) const override;
    void activated(int region) override;
    void paintEvent(QPaintEvent *) override;

private:
    QList<CoverSpec> covers_;
};

class ListRow : public KitWidget
{
    Q_OBJECT
public:
    ListRow(const Theme &theme, const ListRowSpec &spec, QWidget *parent);
    QRect badgeRect() const;
signals:
    void tapped();
    void badgeTapped();

protected:
    int regionAt(const QPoint &pos) const override;
    void activated(int region) override;
    void paintEvent(QPaintEvent *) override;

private:
    ListRowSpec spec_;
};

class SegmentedProgress : public KitWidget
{
    Q_OBJECT
public:
    SegmentedProgress(const Theme &theme, int total, int done, QWidget *parent);
    void setProgress(int total, int done);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    int total_;
    int done_;
};

class CoverStack : public KitWidget
{
    Q_OBJECT
public:
    CoverStack(const Theme &theme, const CoverSpec &front, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    CoverSpec front_;
};

// A modal box over a dimmed page. It covers its parent completely.
class Dialog : public KitWidget
{
    Q_OBJECT
public:
    Dialog(const Theme &theme, const QString &text, const QStringList &actions, QWidget *parent);
    QRect boxRect() const;
signals:
    void actionTapped(int index);

protected:
    int regionAt(const QPoint &pos) const override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private slots:
    void onActionTapped();

private:
    void layoutBox();
    class Box;
    Box *box_;
    QList<Button *> buttons_;
};

} // namespace kit
} // namespace ui
} // namespace smalt
