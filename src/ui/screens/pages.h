// The screens themselves (SPEC, "Home screen and library", "Settings screen").
#pragma once

#include "shell.h"
#include "smalt/settings.h"
#include "smalt/sync.h"

#include <QStringList>
#include <QVector>

#include <functional>

namespace smalt {
namespace screens {

// --- Helpers shared by the screens ---

// A kit list row for a book, with its taps wired: the row opens the book
// detail, the download badge starts the download. Unavailable (greyed, no
// badge) while offline and not on the Kobo.
QWidget *makeBookRow(Shell *shell, const BookItem &book, const QString &subtitle, QWidget *parent);
// "Ada Quill · Book 2" and the like.
QString bookSubtitle(Shell *shell, const BookItem &book, bool withSeries);
// Applies a remembered selection to a kit chip row (the kit's ChipRow).
void setChipSelection(QWidget *chipRow, const QList<int> &selected);
// Marks a block as one that should stay with the next block.
void keepWithNext(QWidget *block);
void setSortKey(QWidget *block, const QString &key);

// --- Tab roots ---

class HomeScreen : public ColumnScreen
{
    Q_OBJECT
public:
    explicit HomeScreen(Shell *shell, QWidget *parent);

    QString title() const override { return QStringLiteral("Home"); }

    bool showsStatusLine() const override { return true; }

    QList<QWidget *> buildBlocks() override;

private slots:
    void onCoverTapped(int index);
    void onReview();
    void onChip(int index, bool selected);

private:
    QVector<BookItem> m_readingNow;
};

class LibraryScreen : public ColumnScreen
{
    Q_OBJECT
public:
    explicit LibraryScreen(Shell *shell, QWidget *parent);

    QString title() const override { return QStringLiteral("Library"); }

    QList<QWidget *> buildBlocks() override;
    QList<QWidget *> buildTop() override;
    LibraryFilter filter() const;

private slots:
    void onChipA(int index, bool selected);
    void onChipB(int index, bool selected);
    void onClear();
};

class SeriesScreen : public ColumnScreen
{
    Q_OBJECT
public:
    explicit SeriesScreen(Shell *shell, QWidget *parent);

    QString title() const override { return QStringLiteral("Series"); }

    QList<QWidget *> buildBlocks() override;
    QList<QWidget *> buildTop() override;

private slots:
    void onChip(int index, bool selected);
};

class StatsScreen : public Screen
{
    Q_OBJECT
public:
    explicit StatsScreen(Shell *shell, QWidget *parent);

    QString title() const override { return QStringLiteral("Stats"); }

    void rebuild() override;

protected:
    void resizeEvent(QResizeEvent *) override;

private:
    QWidget *m_content = nullptr;
};

class MoreScreen : public ColumnScreen
{
    Q_OBJECT
public:
    explicit MoreScreen(Shell *shell, QWidget *parent);

    QString title() const override { return QStringLiteral("More"); }

    bool showsStatusLine() const override { return true; }

    QList<QWidget *> buildBlocks() override;

private slots:
    void onKoboHome();
    void onShelves();
    void onSettings();
    void onSyncStatus();
};

// --- Pushed screens ---

class SeriesDetailScreen : public ColumnScreen
{
    Q_OBJECT
public:
    SeriesDetailScreen(Shell *shell, qint64 seriesId, QWidget *parent);
    QString title() const override;

    QList<QWidget *> buildBlocks() override;

private slots:
    void onContinue();
    void onDownloadNext();

private:
    bool findSeries(SeriesItem *out) const;
    QVector<BookItem> nextToDownload(const SeriesItem &s, int count) const;
    qint64 m_seriesId;
};

class BookDetailScreen : public ColumnScreen
{
    Q_OBJECT
public:
    BookDetailScreen(Shell *shell, const BookItem &book, QWidget *parent);

    QString title() const override { return QStringLiteral("Book"); }

    QList<QWidget *> buildBlocks() override;
    void rebuild() override;

    qint64 bookId() const { return m_book.id; }

private slots:
    void onMainButton();
    void onStatus();
    void onRating();

private:
    enum MainAction { ActionDownload, ActionOpen, ActionUpdate, ActionNone };

    void loadDetail();
    bool updateAvailable() const;
    void setStatus(ReadStatus status);
    void setRating(int rating);
    BookItem m_book;
    QJsonObject m_detail;
    bool m_detailRequested = false;
    MainAction m_action = ActionNone;
};

class SearchScreen : public ColumnScreen
{
    Q_OBJECT
public:
    SearchScreen(Shell *shell, const QString &query, QWidget *parent);

    QString title() const override { return QStringLiteral("Search"); }

    QList<QWidget *> buildBlocks() override;
    QList<QWidget *> buildTop() override;

    QString query() const { return m_query; }

public slots:
    void setQuery(const QString &query);

private slots:
    void onFieldTapped();
    void onSeriesTapped();
    void onAuthorTapped();

private:
    void runOnline();
    QString m_query;
    QVector<BookItem> m_onlineHits;
    bool m_onlineDone = false;
    int m_generation = 0;
};

class ShelvesScreen : public ColumnScreen
{
    Q_OBJECT
public:
    explicit ShelvesScreen(Shell *shell, QWidget *parent);

    QString title() const override { return QStringLiteral("Shelves"); }

    QList<QWidget *> buildBlocks() override;

private slots:
    void onShelfTapped();

private:
    struct Shelf
    {
        QString title;
        qint64 collectionId = 0;
    };

    void load();
    void parse(const QByteArray &json);
    QVector<Shelf> m_shelves;
    bool m_loaded = false;
    bool m_failed = false;
};

class BookListScreen : public ColumnScreen
{
    Q_OBJECT
public:
    BookListScreen(Shell *shell, const QString &title, const QVector<BookItem> &books,
                   QWidget *parent);

    QString title() const override { return m_title; }

    QList<QWidget *> buildBlocks() override;

private:
    QString m_title;
    QVector<BookItem> m_books;
};

class ChoiceScreen : public ColumnScreen
{
    Q_OBJECT
public:
    ChoiceScreen(Shell *shell, const QString &title, const QStringList &options, int current,
                 std::function<void(int)> chosen, QWidget *parent);

    QString title() const override { return m_title; }

    QList<QWidget *> buildBlocks() override;

private slots:
    void onRow();

private:
    QString m_title;
    QStringList m_options;
    int m_current;
    std::function<void(int)> m_chosen;
};

class SyncStatusScreen : public ColumnScreen
{
    Q_OBJECT
public:
    explicit SyncStatusScreen(Shell *shell, QWidget *parent);

    QString title() const override { return QStringLiteral("Sync status"); }

    QList<QWidget *> buildBlocks() override;

private slots:
    void onSyncNow();
    void onProgress(const QString &text);
    void onProgressRefresh();
    void onFinished(const smalt::SyncResult &result);

private:
    QString m_message;
};

class SettingsScreen : public ColumnScreen
{
    Q_OBJECT
public:
    explicit SettingsScreen(Shell *shell, QWidget *parent);

    QString title() const override { return QStringLiteral("Settings"); }

    QList<QWidget *> buildBlocks() override;

    QString testMessage() const { return m_testMessage; }

private slots:
    void onServerUrl();
    void onUsername();
    void onPassword();
    void onTestConnection();
    void onToggle();
    void onBattery();
    void onPushMode();
    void onPushInterval();
    void onNewSession();
    void onDownloadFolder();
    void onExportLog();

private:
    QWidget *toggleRow(const QString &title, bool value, const char *key);
    QWidget *rowWith(const QString &title, const QString &value, const char *slot);
    void save(const Settings &s);
    void applyLogin();
    ClientConfig clientConfigFor(const QString &url, const QString &user,
                                 const QString &password) const;

    QString m_testMessage;
};

} // namespace screens
} // namespace smalt
