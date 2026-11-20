#include "shell.h"

#include "copy.h"
#include "pages.h"
#include "smalt/ledger.h"
#include "smalt/log.h"
#include "smalt/nickel.h"
#include "smalt/settings.h"
#include "smalt/sync.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>

namespace smalt {
namespace screens {

using namespace ui::kit;

QStringList tabNames()
{
    return QStringList() << QStringLiteral("Home") << QStringLiteral("Library")
                         << QStringLiteral("Series") << QStringLiteral("Stats")
                         << QStringLiteral("More");
}

// ---- The text entry page ----

namespace {

// A full-screen page with one line of text and Done / Cancel. The keyboard is
// Nickel's: on the device the line edit takes focus and Nickel's input method
// shows it (a device check). On the desktop the real keyboard types into it.
class TextEntryPage : public QWidget
{
    Q_OBJECT
public:
    TextEntryPage(const Look &look, const QString &title, const QString &initial, bool password,
                  QWidget *parent)
        : QWidget(parent)
    {
        setAutoFillBackground(true);
        QPalette pal = palette();
        pal.setColor(QPalette::Window, look.palette().paper);
        setPalette(pal);
        QVBoxLayout *column = new QVBoxLayout(this);
        const int m = look.margin();
        column->setContentsMargins(m, look.px(14.0), m, m);
        column->setSpacing(look.px(5.0));

        QLabel *label = new QLabel(title, this);
        label->setFont(look.font(FontRole::Title));
        QPalette lp = label->palette();
        lp.setColor(QPalette::WindowText, look.palette().ink);
        label->setPalette(lp);
        column->addWidget(label);

        m_edit = new QLineEdit(initial, this);
        m_edit->setObjectName(QStringLiteral("promptEdit"));
        m_edit->setFont(look.font(FontRole::Body));
        if (password)
            m_edit->setEchoMode(QLineEdit::Password);
        m_edit->setStyleSheet(
                QStringLiteral("QLineEdit { border: 2px solid %1; background: %2; color: %1; "
                               "padding: %3px; }")
                        .arg(look.palette().ink.name(), look.palette().paper.name())
                        .arg(look.px(2.0)));
        m_edit->setMinimumHeight(look.px(12.0));
        column->addWidget(m_edit);

        QHBoxLayout *buttons = new QHBoxLayout();
        buttons->setSpacing(look.px(4.0));
        QWidget *cancel = look.kit()->outlineButton(QStringLiteral("Cancel"), this);
        QWidget *done = look.kit()->primaryButton(QStringLiteral("Done"), this);
        cancel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        done->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(cancel, SIGNAL(tapped()), this, SLOT(onCancel()));
        connect(done, SIGNAL(tapped()), this, SLOT(onDone()));
        buttons->addWidget(cancel);
        buttons->addWidget(done);
        column->addLayout(buttons);
        column->addStretch(1);
        connect(m_edit, SIGNAL(returnPressed()), this, SLOT(onDone()));
    }

    QLineEdit *edit() const { return m_edit; }

    void focusEdit()
    {
        m_edit->setFocus();
        m_edit->selectAll();
    }

signals:
    void done(const QString &text, bool accepted);

private slots:

    void onDone() { emit done(m_edit->text(), true); }

    void onCancel() { emit done(QString(), false); }

private:
    QLineEdit *m_edit;
};

} // namespace

// ---- Screen and ColumnScreen ----

Screen::Screen(Shell *shell, QWidget *parent) : QWidget(parent), m_shell(shell)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, shell->look().palette().paper);
    setPalette(pal);
}

ColumnScreen::ColumnScreen(Shell *shell, QWidget *parent) : Screen(shell, parent)
{
    m_column = new PagedColumn(shell->look(), this);
}

bool ColumnScreen::flipPage(int delta)
{
    return m_column->flip(delta);
}

void ColumnScreen::rebuild()
{
    const QList<QWidget *> top = buildTop();
    if (!top.isEmpty() || !m_top.isEmpty()) {
        for (QWidget *w : m_top) {
            w->hide();
            w->deleteLater();
        }
        m_top = top;
        for (QWidget *w : m_top) {
            w->setParent(this);
            w->show();
        }
    }
    layoutParts();
    m_column->setBlocks(buildBlocks(), true);
}

void ColumnScreen::layoutParts()
{
    int y = 0;
    for (QWidget *w : m_top) {
        w->setGeometry(0, y, width(), w->height());
        y += w->height();
    }
    m_column->setGeometry(0, y, width(), qMax(0, height() - y));
}

void ColumnScreen::resizeEvent(QResizeEvent *)
{
    layoutParts();
}

// ---- Shell ----

Shell::Shell(const ShellDeps &deps, QWidget *parent) : QWidget(parent), m_deps(deps)
{
    if (!m_deps.kit) {
        const DeviceModel model =
                m_deps.nickel ? m_deps.nickel->deviceModel() : DeviceModel::ClaraColour;
        m_ownedKit = ui::createComponentFactory(model, nullptr);
        m_deps.kit = m_ownedKit.get();
    }
    m_look.reset(new Look(m_deps.kit));
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, m_look->palette().paper);
    setPalette(pal);
    setFocusPolicy(Qt::StrongFocus);

    m_catalog = new Catalog(m_deps, this);
    m_catalog->loadFromCache();
    m_downloads = new DownloadManager(m_deps, m_catalog, this);

    connect(m_catalog, SIGNAL(indexChanged()), this, SLOT(onCatalogChanged()));
    connect(m_catalog, SIGNAL(coversChanged()), this, SLOT(onCatalogChanged()));
    connect(m_catalog, SIGNAL(onlineChanged()), this, SLOT(onCatalogChanged()));
    connect(m_downloads, SIGNAL(changed()), this, SLOT(onCatalogChanged()));
    connect(m_downloads, SIGNAL(failed(qint64, QString)), this, SLOT(onSyncChanged()));
    connect(m_downloads, SIGNAL(finished(qint64, QString)), this, SLOT(onDownloadFinished()));
    if (m_deps.sync) {
        connect(m_deps.sync, SIGNAL(statusChanged()), this, SLOT(onSyncChanged()));
        connect(m_deps.sync, SIGNAL(finished(smalt::SyncResult)), this, SLOT(onSyncFinished()));
    }
    if (m_deps.nickel)
        connect(m_deps.nickel, SIGNAL(networkChanged(bool)), this, SLOT(onNetworkChanged(bool)));

    for (int i = 0; i < 5; ++i)
        m_stacks.append(QList<Screen *>());
    m_stacks[0].append(new HomeScreen(this, this));
    m_stacks[1].append(new LibraryScreen(this, this));
    m_stacks[2].append(new SeriesScreen(this, this));
    m_stacks[3].append(new StatsScreen(this, this));
    m_stacks[4].append(new MoreScreen(this, this));
    for (const QList<Screen *> &stack : m_stacks)
        stack.first()->hide();

    rebuildTabs();
    showTop();
    QTimer::singleShot(0, this, SLOT(refresh()));
}

Shell::~Shell() { }

Screen *Shell::currentScreen() const
{
    const QList<Screen *> &stack = m_stacks.value(m_tab);
    return stack.isEmpty() ? nullptr : stack.last();
}

QString Shell::statusLineText() const
{
    QString sync;
    if (m_deps.sync)
        sync = copy::statusLine(m_deps.sync->status(), true);
    else
        sync = copy::statusLine(SyncStatus(), false);
    // The catalog's view of the network wins when there is no engine.
    if (!m_deps.sync && !m_catalog->online())
        sync = QStringLiteral("Offline");
    const QString downloading = m_downloads->statusText();
    if (!downloading.isEmpty())
        return downloading + QStringLiteral(" · ") + sync;
    return sync;
}

void Shell::rebuildTabs()
{
    if (m_tabs) {
        m_tabs->hide();
        m_tabs->deleteLater();
    }
    m_tabs = m_deps.kit->bottomTabs(tabNames(), m_tab, this);
    connect(m_tabs, SIGNAL(tabTapped(int)), this, SLOT(onTabTapped(int)));
    m_tabs->show();
    relayout();
}

void Shell::updateChrome()
{
    Screen *s = currentScreen();
    if (!s)
        return;
    const QString title = s->title();
    if (!m_header || title != m_lastTitle) {
        if (m_header) {
            m_header->hide();
            m_header->deleteLater();
        }
        m_header = m_deps.kit->headerBar(title, this);
        connect(m_header, SIGNAL(searchTapped()), this, SLOT(onSearchTapped()));
        m_header->show();
        m_lastTitle = title;
    }
    const bool showStatus = s->showsStatusLine();
    const QString text = statusLineText();
    if (showStatus && (!m_status || text != m_lastStatusText)) {
        if (m_status) {
            m_status->hide();
            m_status->deleteLater();
        }
        m_status = m_deps.kit->statusLine(text, this);
        connect(m_status, SIGNAL(tapped()), this, SLOT(onStatusTapped()));
        m_status->show();
        m_lastStatusText = text;
    } else if (!showStatus && m_status) {
        m_status->hide();
        m_status->deleteLater();
        m_status = nullptr;
    }
    const bool showBack = depth() > 1;
    if (showBack && !m_back) {
        BackBar *b = new BackBar(*m_look, this);
        connect(b, SIGNAL(tapped()), this, SLOT(onBackTapped()));
        b->show();
        m_back = b;
    } else if (!showBack && m_back) {
        m_back->hide();
        m_back->deleteLater();
        m_back = nullptr;
    }
    relayout();
}

void Shell::relayout()
{
    int y = 0;
    const int w = width();
    if (m_header) {
        m_header->setGeometry(0, y, w, m_header->height());
        y += m_header->height();
    }
    if (m_status) {
        m_status->setGeometry(0, y, w, m_status->height());
        y += m_status->height();
    }
    if (m_back) {
        m_back->setGeometry(0, y, w, m_back->height());
        y += m_back->height();
    }
    int bottom = height();
    if (m_tabs) {
        bottom -= m_tabs->height();
        m_tabs->setGeometry(0, bottom, w, m_tabs->height());
    }
    if (Screen *s = currentScreen())
        s->setGeometry(0, y, w, qMax(0, bottom - y));
    if (m_dialog)
        m_dialog->setGeometry(rect());
    if (m_prompt)
        m_prompt->setGeometry(rect());
}

void Shell::resizeEvent(QResizeEvent *)
{
    relayout();
}

void Shell::showTop()
{
    for (const QList<Screen *> &stack : m_stacks)
        for (Screen *s : stack)
            s->hide();
    Screen *top = currentScreen();
    if (!top)
        return;
    updateChrome();
    top->show();
    top->raise();
    top->rebuild();
    if (m_dialog)
        m_dialog->raise();
    m_look->theme().requestFull(this); // A screen change is a full refresh.
}

void Shell::switchTab(int index)
{
    if (index < 0 || index >= m_stacks.size())
        return;
    if (index == m_tab) {
        if (depth() > 1) {
            while (depth() > 1)
                pop();
        }
        return;
    }
    m_tab = index;
    rebuildTabs();
    showTop();
    emit tabChanged(m_tab);
}

void Shell::onTabTapped(int index)
{
    if (index == m_tab)
        return;
    m_tab = index;
    showTop();
    emit tabChanged(m_tab);
}

void Shell::push(Screen *screen)
{
    screen->setParent(this);
    m_stacks[m_tab].append(screen);
    showTop();
}

void Shell::pop()
{
    QList<Screen *> &stack = m_stacks[m_tab];
    if (stack.size() <= 1)
        return;
    Screen *gone = stack.takeLast();
    gone->hide();
    gone->deleteLater();
    showTop();
}

void Shell::onBackTapped()
{
    pop();
}

void Shell::onSearchTapped()
{
    openSearch(QString(), true);
}

void Shell::onStatusTapped()
{
    // The status line leads to the sync details under More.
    switchTab(4);
    while (depth() > 1)
        pop();
    openSyncStatus();
}

void Shell::onSyncChanged()
{
    updateChrome();
}

void Shell::onSyncFinished()
{
    m_catalog->invalidateLocal();
    m_catalog->refresh();
    if (Screen *s = currentScreen())
        s->rebuild();
    updateChrome();
}

void Shell::onDownloadFinished()
{
    // After the last book of a batch: say so if Nickel couldn't be asked to import.
    if (!m_downloads->busy() && !m_downloads->lastNote().isEmpty())
        showMessage(m_downloads->lastNote());
}

void Shell::onNetworkChanged(bool connected)
{
    m_catalog->setOnline(connected, connected ? ErrorKind::None : ErrorKind::Offline);
    if (connected)
        m_catalog->refresh();
}

void Shell::onCatalogChanged()
{
    if (Screen *s = currentScreen())
        s->rebuild();
    updateChrome();
}

void Shell::refresh()
{
    m_catalog->invalidateLocal();
    m_catalog->refresh();
    if (Screen *s = currentScreen())
        s->rebuild();
    updateChrome();
}

bool Shell::flipPage(int delta)
{
    if (m_dialog || m_prompt)
        return false;
    Screen *s = currentScreen();
    return s ? s->flipPage(delta) : false;
}

void Shell::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_PageDown) {
        flipPage(1);
        event->accept();
    } else if (event->key() == Qt::Key_PageUp) {
        flipPage(-1);
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}

// ---- Navigation helpers ----

void Shell::openBook(qint64 bookId)
{
    BookItem item;
    if (const BookItem *b = m_catalog->find(bookId))
        item = *b;
    else if (m_extra.contains(bookId))
        item = m_extra.value(bookId);
    else
        item.id = bookId;
    push(new BookDetailScreen(this, item, this));
}

void Shell::openSeries(qint64 seriesId)
{
    push(new SeriesDetailScreen(this, seriesId, this));
}

void Shell::openSearch(const QString &query, bool promptForText)
{
    SearchScreen *search = qobject_cast<SearchScreen *>(currentScreen());
    const bool fresh = search == nullptr;
    if (fresh) {
        search = new SearchScreen(this, query, this);
        push(search);
    } else if (!query.isEmpty()) {
        search->setQuery(query);
    }
    if (!promptForText)
        return;
    QPointer<SearchScreen> guard(search);
    promptText(QStringLiteral("Search"), search->query(), false,
               [this, guard, fresh](const QString &text, bool accepted) {
                   if (!guard)
                       return;
                   if (accepted && !text.trimmed().isEmpty())
                       guard->setQuery(text.trimmed());
                   else if (fresh && guard->query().isEmpty())
                       pop();
               });
}

void Shell::openShelves()
{
    push(new ShelvesScreen(this, this));
}

void Shell::openSettings()
{
    push(new SettingsScreen(this, this));
}

void Shell::openSyncStatus()
{
    push(new SyncStatusScreen(this, this));
}

void Shell::openChoice(const QString &title, const QStringList &options, int current,
                       std::function<void(int)> chosen)
{
    push(new ChoiceScreen(this, title, options, current, chosen, this));
}

void Shell::openBookList(const QString &title, const QVector<BookItem> &books)
{
    push(new BookListScreen(this, title, books, this));
}

// ---- Actions ----

void Shell::startDownload(const BookItem &book)
{
    if (m_deps.nickel && !m_deps.nickel->networkConnected()) {
        showMessage(copy::offlineDownload());
        return;
    }
    if (m_catalog->onKobo(book.id)) {
        // Already here: only "Update available" re-fetches, and it asks for that itself.
        return;
    }
    m_downloads->enqueue(book);
}

void Shell::startDownloads(const QVector<BookItem> &books)
{
    if (m_deps.nickel && !m_deps.nickel->networkConnected()) {
        showMessage(copy::offlineDownload());
        return;
    }
    m_downloads->enqueue(books);
}

void Shell::openInReader(qint64 bookId)
{
    BookMapEntry e;
    if (!m_catalog->bookMap(bookId, &e)) {
        showMessage(copy::openFailed());
        return;
    }
    if (!m_deps.nickel) {
        showMessage(copy::openFailedNotReady());
        return;
    }
    const Status s = m_deps.nickel->openBook(e.contentId);
    if (s.ok())
        return;
    log::info("screens", QStringLiteral("open book failed: %1").arg(s.message));
    switch (s.code) {
    case StatusCode::NotImplemented:
    case StatusCode::Unavailable:
        showMessage(copy::openFailedNotReady());
        break;
    case StatusCode::NotFound:
        showMessage(copy::openFailedNotFound());
        break;
    default:
        showMessage(copy::openFailed());
        break;
    }
}

void Shell::showKoboHome()
{
    if (!m_deps.nickel) {
        showMessage(copy::koboHomeNotReady());
        return;
    }
    const Status s = m_deps.nickel->showNickelHome();
    if (!s.ok())
        showMessage(s.code == StatusCode::NotImplemented ? copy::koboHomeNotReady()
                                                         : copy::openFailed());
}

void Shell::syncNow()
{
    if (!m_deps.sync) {
        showMessage(copy::syncNotAvailable());
        return;
    }
    SyncRequest r;
    r.trigger = SyncTrigger::Manual;
    m_deps.sync->request(r);
}

void Shell::showMessage(const QString &text)
{
    showDialog(text, QStringList() << QStringLiteral("OK"), std::function<void(int)>());
}

void Shell::showDialog(const QString &text, const QStringList &actions,
                       std::function<void(int)> chosen)
{
    if (m_dialog) {
        m_dialog->hide();
        m_dialog->deleteLater();
        m_dialog = nullptr;
    }
    m_dialogDone = chosen;
    m_dialog = m_deps.kit->dialog(text, actions, this);
    connect(m_dialog, SIGNAL(actionTapped(int)), this, SLOT(onDialogAction(int)));
    m_dialog->setGeometry(rect());
    m_dialog->show();
    m_dialog->raise();
}

void Shell::onDialogAction(int index)
{
    std::function<void(int)> done = m_dialogDone;
    m_dialogDone = std::function<void(int)>();
    if (m_dialog) {
        m_dialog->hide();
        m_dialog->deleteLater();
        m_dialog = nullptr;
    }
    m_look->theme().requestFull(this);
    if (done)
        done(index);
}

void Shell::promptText(const QString &title, const QString &initial, bool password,
                       std::function<void(const QString &, bool)> done)
{
    if (m_prompt) {
        m_prompt->hide();
        m_prompt->deleteLater();
    }
    m_promptDone = done;
    TextEntryPage *page = new TextEntryPage(*m_look, title, initial, password, this);
    connect(page, SIGNAL(done(QString, bool)), this, SLOT(onPromptDone(QString, bool)));
    page->setGeometry(rect());
    page->show();
    page->raise();
    page->focusEdit();
    m_prompt = page;
    m_look->theme().requestFull(this);
}

void Shell::onPromptDone(const QString &text, bool accepted)
{
    std::function<void(const QString &, bool)> done = m_promptDone;
    m_promptDone = std::function<void(const QString &, bool)>();
    if (m_prompt) {
        m_prompt->hide();
        m_prompt->deleteLater();
        m_prompt = nullptr;
    }
    m_look->theme().requestFull(this);
    if (done)
        done(text, accepted);
}

int Shell::pendingRemovals() const
{
    if (!m_deps.ledger)
        return 0;
    int n = 0;
    const QVector<PendingDecision> all = m_deps.ledger->pendingDecisions();
    for (const PendingDecision &d : all)
        if (d.choice == PendingChoice::Undecided || d.choice == PendingChoice::DecideLater)
            ++n;
    return n;
}

void Shell::requestRemovalReview()
{
    if (receivers(SIGNAL(reviewRemovalsRequested())) > 0)
        emit reviewRemovalsRequested();
    else
        showMessage(copy::reviewNotReady());
}

} // namespace screens

// ---- Public entry point (include/smalt/screens.h) ----

QWidget *createCompanionShell(const ShellDeps &deps, QWidget *parent)
{
    return new screens::Shell(deps, parent);
}

} // namespace smalt

#include "shell.moc"
