// Shared look and small widgets of the screens.
//
// Everything visible is built from the UI kit's ComponentFactory where the kit
// has the component. The few things the kit has no component for (a plain text
// row for Settings and More, a block of wrapped text, a back bar, and the page
// layout that flows blocks into pages) are drawn here with the kit's own fonts,
// palette and millimetre metrics, so they look like the kit. Proposal: move
// TextRow, TextBlock and BackBar into the kit.
#pragma once

#include "kit/factory.h"
#include "smalt/ui_kit.h"

#include <QFont>
#include <QList>
#include <QString>
#include <QVariant>
#include <QWidget>

#include <functional>

namespace smalt {
namespace screens {

class Look
{
public:
    explicit Look(ui::ComponentFactory *kit);

    ui::ComponentFactory *kit() const { return m_kit; }

    const DeviceProfile &profile() const { return m_kit->profile(); }

    const ui::Palette &palette() const { return m_kit->palette(); }

    const ui::kit::Theme &theme() const { return m_theme; }

    int px(double mm) const { return ui::mmToPx(mm, profile()); }

    QFont font(ui::kit::FontRole role) const { return m_theme.font(role); }

    int margin() const { return px(ui::kit::metrics::kPageMarginMm); }

    // Width of `text` in `font` (QFontMetrics::width is the only call on Qt 5.2).
    static int textWidth(const QFont &font, const QString &text);

private:
    ui::ComponentFactory *m_kit;
    ui::kit::Theme m_theme;
};

// A tappable text row with a title and a value or subtitle, separated by a thin
// rule (Settings, More, choices). Single tap emits tapped().
class TextRow : public QWidget
{
    Q_OBJECT
public:
    TextRow(const Look &look, const QString &title, const QString &value, QWidget *parent);
    void setTitle(const QString &title);
    void setValue(const QString &value);
    void setSubtitle(const QString &subtitle); // A second line under the title.
    void setDimmed(bool dimmed); // Drawn in Secondary, as unavailable items are.

    QString title() const { return m_title; }

    QString value() const { return m_value; }

signals:
    void tapped();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    Look m_look;
    QString m_title;
    QString m_value;
    QString m_subtitle;
    bool m_dimmed = false;
    bool m_pressed = false;
};

// Wrapped text: a heading line, a note or a description. Height follows width.
class TextBlock : public QWidget
{
    Q_OBJECT
public:
    enum Style { Body, Small, Title, Secondary };

    TextBlock(const Look &look, const QString &text, Style style, QWidget *parent);
    void setText(const QString &text);

    QString text() const { return m_text; }

    bool hasHeightForWidth() const override { return true; }

    int heightForWidth(int w) const override;

    // Splits `text` into blocks of at most `maxLines` lines at width `width`,
    // so a long description can flow across pages.
    static QList<TextBlock *> split(const Look &look, const QString &text, Style style, int width,
                                    int maxLines, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QFont fontFor() const;
    QColor colourFor() const;

    Look m_look;
    QString m_text;
    Style m_style;
};

// "‹ Back" bar at the top of a pushed screen.
class BackBar : public QWidget
{
    Q_OBJECT
public:
    BackBar(const Look &look, QWidget *parent);

signals:
    void tapped();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    Look m_look;
    bool m_pressed = false;
};

// A block of known height whose content is only built while it is on screen.
// A long list (hundreds of books) is a column of these, so only the rows on the
// current page cost anything (covers, ledger lookups).
class LazyBlock : public QWidget
{
    Q_OBJECT
public:
    typedef std::function<QWidget *(QWidget *parent)> Factory;

    LazyBlock(int heightPx, const Factory &factory, QWidget *parent);

    bool built() const { return m_inner != nullptr; }

protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    Factory m_factory;
    QWidget *m_inner = nullptr;
};

// Children stacked at full width as one block, so a short section (a header and
// its rows) moves to the next page together instead of splitting.
class StackBlock : public QWidget
{
    Q_OBJECT
public:
    explicit StackBlock(QWidget *parent);
    // `child` must be a child of this block (create it with this as parent).
    void add(QWidget *child);

    bool hasHeightForWidth() const override { return true; }

    int heightForWidth(int w) const override;

protected:
    void resizeEvent(QResizeEvent *) override;

private:
    void layoutChildren();
    QList<QWidget *> m_children;
};

// Buttons stacked full width with the kit's spacing, one block.
class ButtonStack : public QWidget
{
    Q_OBJECT
public:
    ButtonStack(const Look &look, QWidget *parent);
    // `button` must be a child of this stack.
    void add(QWidget *button);

protected:
    void resizeEvent(QResizeEvent *) override;

private:
    void layoutButtons();
    Look m_look;
    QList<QWidget *> m_buttons;
};

// Flows blocks (widgets with a known height) into pages that fit the height of
// this widget, with a "Page 1 of 54" pager at the bottom when there is more
// than one page. Tapping the counter opens the jump-to-letter grid when enabled.
//
// Block properties: "keepWithNext" (bool) keeps a block on the page of the
// next one (section headers); "sortKey" (string) is what the jump-to-letter
// grid compares.
class PagedColumn : public QWidget
{
    Q_OBJECT
public:
    PagedColumn(const Look &look, QWidget *parent);

    // Takes ownership of `blocks` and replaces the previous ones. The current
    // page is kept when `keepPage` is true (clamped).
    void setBlocks(const QList<QWidget *> &blocks, bool keepPage = false);

    int page() const { return m_page; } // 0-based

    int pageCount() const { return qMax(1, m_pages.size()); }

    bool pagerShown() const { return m_pagerShown; }

    void setPage(int page);
    bool flip(int delta); // True if the page changed.

    void setLetterJump(bool enabled) { m_letterJump = enabled; }

    // The block widgets on the current page, top to bottom.
    QList<QWidget *> visibleBlocks() const;

    QList<QWidget *> blocks() const { return m_blocks; }

    // First page whose first sortKey is at or after `letter` ("#": before A).
    int pageForLetter(const QString &letter) const;

    // Height the blocks get on one page, for tests.
    int usableHeight() const;

signals:
    void pageChanged(int page, int pageCount);

protected:
    void resizeEvent(QResizeEvent *) override;

private slots:
    void onPrevious();
    void onNext();
    void onCounter();
    void onLetter(const QString &letter);
    void closeLetters();

private:
    int blockHeight(QWidget *w) const;
    QList<QPair<int, int>> paginate(int availableHeight) const;
    void layoutPage();

    Look m_look;
    QList<QWidget *> m_blocks;
    QList<QPair<int, int>> m_pages; // [first, last) block indexes.
    int m_page = 0;
    bool m_pagerShown = false;
    bool m_letterJump = false;
    QWidget *m_pager = nullptr;
    QWidget *m_letters = nullptr;
};

} // namespace screens
} // namespace smalt
