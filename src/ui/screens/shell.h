// The companion shell: header with search, status line, five tabs and the
// navigation inside each tab. Screens talk to it through this class.
//
// Task 30 shows this widget in place of Nickel's home. It routes the Libra's
// page-turn buttons to flipPage(), and the desktop uses PageUp and PageDown.
// The removal banner's "Review" emits reviewRemovalsRequested(); task 33 shows
// its review screen from that signal (connect with the string-based syntax on
// the widget createCompanionShell() returns). Without a listener the banner
// says the review isn't available yet.
#pragma once

#include "catalog.h"
#include "downloads.h"
#include "look.h"
#include "smalt/screens.h"

#include <QHash>
#include <QList>
#include <QWidget>

#include <functional>
#include <memory>

namespace smalt {
namespace screens {

class Shell;

// One screen: a tab's root or a pushed page.
class Screen : public QWidget
{
    Q_OBJECT
public:
    Screen(Shell *shell, QWidget *parent);

    virtual QString title() const = 0;

    // Rebuilds the screen from current data. Called when it is shown, after
    // data changed, and after a resize.
    virtual void rebuild() { }

    virtual bool flipPage(int) { return false; }

    // The status line ("Synced 10:42 · 3 waiting") sits under the header on Home and More.
    virtual bool showsStatusLine() const { return false; }

protected:
    Shell *shell() const { return m_shell; }

private:
    Shell *m_shell;
};

// A screen made of optional fixed widgets at the top (chips, search field) and
// a paged column below them.
class ColumnScreen : public Screen
{
    Q_OBJECT
public:
    ColumnScreen(Shell *shell, QWidget *parent);

    bool flipPage(int delta) override;

    PagedColumn *column() const { return m_column; }

    // Blocks of the column: widgets with a known height. Called by rebuild().
    virtual QList<QWidget *> buildBlocks() = 0;

    // Widgets above the column (rebuilt with the blocks when `rebuildTop` says so).
    virtual QList<QWidget *> buildTop() { return QList<QWidget *>(); }

    void rebuild() override;

protected:
    void resizeEvent(QResizeEvent *) override;
    void layoutParts();

private:
    PagedColumn *m_column;
    QList<QWidget *> m_top;
};

class Shell : public QWidget
{
    Q_OBJECT
public:
    Shell(const ShellDeps &deps, QWidget *parent);
    ~Shell() override;

    // --- Context for screens ---
    const ShellDeps &deps() const { return m_deps; }

    const Look &look() const { return *m_look; }

    Catalog *catalog() const { return m_catalog; }

    DownloadManager *downloads() const { return m_downloads; }

    QString statusLineText() const;

    // Chips and sort remembered per tab for the session. One map per tab key.
    QList<int> &chipMemory(const QString &key) { return m_chips[key]; }

    // --- Navigation ---
    int currentTab() const { return m_tab; }

    Screen *currentScreen() const;

    int depth() const { return m_stacks.value(m_tab).size(); }

    void switchTab(int index);
    void push(Screen *screen); // Takes ownership.
    void pop();

    void openBook(qint64 bookId);
    void openSeries(qint64 seriesId);
    void openSearch(const QString &query, bool promptForText);
    void openShelves();
    void openSettings();
    void openSyncStatus();
    // A page of choices (Sort, status, rating, Settings values): tapping one
    // calls `chosen(index)` and goes back.
    void openChoice(const QString &title, const QStringList &options, int current,
                    std::function<void(int)> chosen);
    // A page listing books (a shelf).
    void openBookList(const QString &title, const QVector<BookItem> &books);

    // --- Actions ---
    void startDownload(const BookItem &book);
    void startDownloads(const QVector<BookItem> &books);
    void openInReader(qint64 bookId);
    void showKoboHome();
    void syncNow();
    void showMessage(const QString &text);
    void showDialog(const QString &text, const QStringList &actions,
                    std::function<void(int)> chosen);
    // Asks for a line of text (the search words, the server address ...). The
    // keyboard is Nickel's; see the report.
    void promptText(const QString &title, const QString &initial, bool password,
                    std::function<void(const QString &text, bool accepted)> done);

    // Remembers a book that came from a search and isn't in the index, so its
    // detail page can open.
    void rememberBook(const BookItem &book) { m_extra.insert(book.id, book); }

    int pendingRemovals() const;
    void requestRemovalReview();

    // --- For tests and task 30 ---
    bool dialogOpen() const { return m_dialog != nullptr; }

    bool promptOpen() const { return m_prompt != nullptr; }

    QWidget *prompt() const { return m_prompt; }

    QWidget *dialogWidget() const { return m_dialog; }

    QWidget *headerWidget() const { return m_header; }

    QWidget *tabsWidget() const { return m_tabs; }

    QWidget *statusWidget() const { return m_status; }

public slots:
    // Flips the current list one page (Libra page-turn buttons, PageUp/PageDown).
    bool flipPage(int delta);
    // Re-reads local state and asks for fresh data.
    void refresh();
    void onSearchTapped();
    void onTabTapped(int index);
    void onBackTapped();
    void onStatusTapped();
    void onSyncChanged();
    void onCatalogChanged();
    void onDialogAction(int index);
    void onSyncFinished();
    void onDownloadFinished();
    void onNetworkChanged(bool connected);
    void onPromptDone(const QString &text, bool accepted);

signals:
    // Task 33 shows the review screen from this.
    void reviewRemovalsRequested();
    // Task 30: the shell wants to leave (never emitted today).
    void tabChanged(int tab);

protected:
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void relayout();
    void updateChrome();
    void rebuildTabs();
    void showTop();

    ShellDeps m_deps;
    std::unique_ptr<ui::ComponentFactory> m_ownedKit;
    std::unique_ptr<Look> m_look;
    Catalog *m_catalog = nullptr;
    DownloadManager *m_downloads = nullptr;

    QList<QList<Screen *>> m_stacks;
    int m_tab = 0;

    QWidget *m_header = nullptr;
    QWidget *m_status = nullptr;
    QWidget *m_back = nullptr;
    QWidget *m_tabs = nullptr;
    QWidget *m_dialog = nullptr;
    QWidget *m_prompt = nullptr;
    std::function<void(int)> m_dialogDone;
    std::function<void(const QString &, bool)> m_promptDone;
    QHash<QString, QList<int>> m_chips;
    QHash<qint64, BookItem> m_extra;
    QString m_lastStatusText;
    QString m_lastTitle;
    bool m_lastShowStatus = false;
    bool m_lastShowBack = false;
};

// The tab names, in order.
QStringList tabNames();

} // namespace screens
} // namespace smalt
