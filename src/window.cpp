#include "window.hpp"
#include "theme.hpp"
#include <QtWidgets>
#include <algorithm>
#include <QDesktopServices>
#include <QWebEngineDownloadRequest>
#include <QWebEngineFindTextResult>
#include <QWebEngineHistory>
#include <QWebEngineLoadingInfo>
#include <QWebEngineNewWindowRequest>
#include <QWebEngineSettings>
#include <QtWebEngineCore/qtwebenginecoreglobal.h>

namespace {
class ExplorerTree final : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    std::function<void(qint64, QString, QString)> moveFavorite;
protected:
    void dropEvent(QDropEvent *event) override
    {
        if (event->source() != this || !moveFavorite)
            return;
        const auto *entry = currentItem();
        if (!entry || !entry->data(0, Qt::UserRole + 1).isValid())
            return;
        const auto *target = itemAt(event->position().toPoint());
        const auto folder = target ? target->data(0, Qt::UserRole + 2).toString() : QString();
        const auto id = entry->data(0, Qt::UserRole + 1).toLongLong();
        const auto title = entry->text(0);
        event->acceptProposedAction();
        QTimer::singleShot(0, this, [this, id, title, folder] { moveFavorite(id, title, folder); });
    }
};
}

Window::Window(Engine &profile, Engine &privateProfile, Store &data, bool offRecord)
    : engine(profile), privateEngine(privateProfile), store(data), privateMode(offRecord),
      tabsEnabled(QSettings().value("tabs", false).toBool())
{
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1040, 740);
    tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    tabs->setTabsClosable(tabsEnabled);
    tabs->setMovable(tabsEnabled);
    tabs->tabBar()->setVisible(tabsEnabled);
    setCentralWidget(tabs);
    buildChrome();
    buildMenus();
    createFindBar();
    connect(tabs, &QTabWidget::currentChanged, this, &Window::updateChrome);
    connect(tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (tabs->count() == 1) {
            close();
            return;
        }
        auto *view = tabs->widget(index);
        tabs->removeTab(index);
        view->deleteLater();
    });
    navigation->setVisible(QSettings().value("showNavigation", true).toBool());
    addressBar->setVisible(QSettings().value("showAddress", true).toBool());
    statusBar()->setVisible(QSettings().value("showStatus", true).toBool());
}

void Window::start(const QUrl &url, bool mainWindow)
{
    QSettings settings;
    managesSession = !privateMode && mainWindow;
    if (managesSession)
        restoreGeometry(settings.value("geometry").toByteArray());
    if (tabsEnabled && managesSession && !url.isValid() && settings.value("restoreTabs", false).toBool()) {
        restoring = true;
        for (const auto &entry : settings.value("session").toStringList()) {
            const QUrl saved(entry);
            if (supportedUrl(saved))
                addPage(saved, true);
        }
        const int selected = settings.value("sessionIndex", 0).toInt();
        if (selected >= 0 && selected < tabs->count())
            tabs->setCurrentIndex(selected);
        restoring = false;
    }
    if (tabs->count() == 0) {
        addPage(url);
        if (!url.isValid())
            home();
    }
}

QWebEngineView *Window::current() const
{
    return tabs->currentWidget() ? tabs->currentWidget()->findChild<QWebEngineView *>("browserView") : nullptr;
}

QWebEngineView *Window::addPage(const QUrl &url, bool background)
{
    if (!tabsEnabled && tabs->count())
        return current();
    auto *container = new QWidget(tabs);
    auto *stack = new QStackedLayout(container);
    auto *view = engine.createView(container);
    view->setObjectName("browserView");
    stack->addWidget(view);
    auto *error = new QWidget(container);
    auto *errorLayout = new QVBoxLayout(error);
    errorLayout->setContentsMargins(36, 32, 36, 32);
    error->setAutoFillBackground(true);
    auto *heading = new QLabel("The page cannot be displayed", error);
    QFont headingFont = heading->font();
    headingFont.setPointSize(19);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    auto *detail = new QLabel(error);
    detail->setTextFormat(Qt::PlainText);
    detail->setWordWrap(true);
    auto *retry = new QPushButton("&Retry", error);
    errorLayout->addWidget(heading);
    errorLayout->addWidget(detail);
    errorLayout->addWidget(retry, 0, Qt::AlignLeft);
    errorLayout->addStretch();
    stack->addWidget(error);
    connect(retry, &QPushButton::clicked, view, [view] {
        const auto url = view->property("failedUrl").toUrl();
        if (supportedUrl(url))
            view->load(url);
    });
    const int index = tabs->addTab(container, toolbarIcon("web"), "Blank Page");
    if (!background || tabs->count() == 1)
        tabs->setCurrentIndex(index);
    view->setZoomFactor(QSettings().value("zoom", 1.0).toDouble());
    connect(view, &QWebEngineView::titleChanged, this, [this, view](const QString &title) {
        tabs->setTabText(tabs->indexOf(view->parentWidget()), title.isEmpty() ? "Blank Page" : title.left(35));
        if (view == current())
            updateChrome();
    });
    connect(view, &QWebEngineView::iconChanged, this, [this, view](const QIcon &icon) {
        tabs->setTabIcon(tabs->indexOf(view->parentWidget()), icon.isNull() ? toolbarIcon("web") : icon);
        if (view == current())
            updateChrome();
    });
    connect(view, &QWebEngineView::urlChanged, this, [this, view](const QUrl &) {
        if (view == current())
            updateChrome();
    });
    connect(view, &QWebEngineView::loadStarted, this, [this, view, stack] {
        stack->setCurrentWidget(view);
        view->setProperty("failedUrl", QVariant());
        view->setProperty("progress", 0);
        view->setProperty("status", "Opening page…");
        if (view == current())
            updateChrome();
    });
    connect(view, &QWebEngineView::loadProgress, this, [this, view](int percent) {
        view->setProperty("progress", percent);
        if (view == current()) {
            progress->setValue(percent);
            progress->show();
        }
    });
    connect(view->page(), &QWebEnginePage::loadingChanged, this, [this, view, stack, error, detail, retry](const QWebEngineLoadingInfo &info) {
        using Load = QWebEngineLoadingInfo;
        if (info.status() == Load::LoadStartedStatus)
            return;
        QString state = "Done";
        if (info.status() == Load::LoadStoppedStatus)
            state = "Stopped";
        if (info.status() == Load::LoadFailedStatus && Engine::hasHttpResponse(info)) {
            state = QString("HTTP %1").arg(info.errorCode());
        } else if (info.status() == Load::LoadFailedStatus && !info.isDownload()) {
            state = "The page cannot be displayed";
            detail->setText(info.url().toDisplayString(QUrl::RemoveUserInfo) + "\n\n" + engine.failure(view));
            view->setProperty("failedUrl", info.url());
            retry->setEnabled(supportedUrl(info.url()));
            stack->setCurrentWidget(error);
        }
        view->setProperty("status", state);
        view->setProperty("progress", -1);
        if (info.status() == Load::LoadSucceededStatus && !privateMode && supportedUrl(info.url()))
            perform([&] {
                store.visit(info.url(), view->title());
                if (explorer->isVisible() && !showingFavorites)
                    populateExplorer();
            });
        if (view == current()) {
            updateChrome();
            updateAddressItems();
        }
    });
    connect(view->page(), &QWebEnginePage::linkHovered, this, [this, view](const QString &link) {
        if (view == current())
            message->setText(link.isEmpty() ? view->property("status").toString() : link);
    });
    connect(view->page(), &QWebEnginePage::newWindowRequested, this, [this](QWebEngineNewWindowRequest &request) {
        if (!request.isUserInitiated() || !supportedUrl(request.requestedUrl()))
            return;
        if (tabsEnabled && request.destination() != QWebEngineNewWindowRequest::InNewWindow)
            request.openIn(addPage({}, request.destination() == QWebEngineNewWindowRequest::InNewBackgroundTab)->page());
        else {
            auto *window = new Window(engine, privateEngine, store, privateMode);
            window->show();
            request.openIn(window->addPage()->page());
        }
    });
    if (supportedUrl(url))
        view->load(url);
    return view;
}

void Window::buildChrome()
{
    navigation = addToolBar("Standard Buttons");
    navigation->setMovable(false);
    navigation->setIconSize(QSize(22, 22));
    navigation->setToolButtonStyle(QSettings().value("labels", true).toBool() ?
                                   Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
    auto button = [this](const QString &label, const QString &icon, const auto &handler) {
        return navigation->addAction(toolbarIcon(icon), label, this, handler);
    };
    backAction = button("Back", "back", [this] { if (auto *view = current()) view->back(); });
    backAction->setShortcut(QKeySequence("Alt+Left"));
    forwardAction = button("Forward", "forward", [this] { if (auto *view = current()) view->forward(); });
    forwardAction->setShortcut(QKeySequence("Alt+Right"));
    stopAction = button("Stop", "stop", [this] { if (auto *view = current()) view->stop(); });
    stopAction->setShortcut(QKeySequence(Qt::Key_Escape));
    button("Refresh", "refresh", [this] { if (auto *view = current()) view->reload(); })
        ->setShortcuts({QKeySequence("F5"), QKeySequence("Ctrl+R")});
    button("Home", "home", [this] { home(); })->setShortcut(QKeySequence("Alt+Home"));
    navigation->addSeparator();
    button("Search", "search", [this] {
        bool ok;
        const auto query = QInputDialog::getText(this, "Web Search", "Search for", QLineEdit::Normal, {}, &ok);
        if (ok && !query.trimmed().isEmpty())
            go(searchUrl(query, QSettings().value("search", "DuckDuckGo").toString()));
    });
    button("Favorites", "favorite", [this] { showExplorer(true); })->setShortcut(QKeySequence("Ctrl+I"));
    button("History", "history", [this] { showExplorer(false); })->setShortcut(QKeySequence("Ctrl+H"));
    navigation->addSeparator();
    button("Print", "print", [this] { printPage(false); })->setShortcut(QKeySequence::Print);

    addToolBarBreak();
    addressBar = addToolBar("Address Bar");
    addressBar->setMovable(false);
    addressBar->setIconSize(QSize(16, 16));
    addressBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *label = new QLabel("&Address", addressBar);
    addressBar->addWidget(label);
    auto *siteIcon = new QLabel(addressBar);
    siteIcon->setObjectName("siteIcon");
    siteIcon->setPixmap(toolbarIcon("web").pixmap(16));
    addressBar->addWidget(siteIcon);
    address = new QComboBox(addressBar);
    address->setEditable(true);
    address->setInsertPolicy(QComboBox::NoInsert);
    address->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    address->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    address->lineEdit()->setClearButtonEnabled(false);
    address->lineEdit()->setAcceptDrops(true);
    label->setBuddy(address->lineEdit());
    addressBar->addWidget(address);
    addressBar->addAction(toolbarIcon("forward"), "Go", this, [this] { navigate(address->currentText()); });
    connect(address->lineEdit(), &QLineEdit::returnPressed, this, [this] { navigate(address->currentText()); });
    connect(address, &QComboBox::textActivated, this, [this](const QString &text) { navigate(text); });
    for (const auto &key : {"Alt+D", "Ctrl+L"}) {
        auto *shortcut = new QShortcut(QKeySequence(key), this);
        connect(shortcut, &QShortcut::activated, this, [this] {
            addressBar->show();
            address->setFocus();
            address->lineEdit()->selectAll();
        });
    }
    updateAddressItems();

    message = new QLabel("Ready", this);
    message->setTextFormat(Qt::PlainText);
    message->setMinimumWidth(100);
    message->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    zone = new QLabel("Local", this);
    zone->setMinimumWidth(136);
    progress = new QProgressBar(this);
    progress->setFixedWidth(120);
    progress->setTextVisible(false);
    auto *statusIcon = new QLabel(this);
    statusIcon->setPixmap(toolbarIcon("web").pixmap(14));
    statusBar()->addWidget(statusIcon);
    statusBar()->addWidget(message, 1);
    statusBar()->addPermanentWidget(progress);
    statusBar()->addPermanentWidget(zone);
    progress->hide();

    explorer = new QDockWidget("Favorites", this);
    explorer->setFeatures(QDockWidget::DockWidgetClosable);
    explorer->setAllowedAreas(Qt::LeftDockWidgetArea);
    auto *panel = new QWidget(explorer);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    filter = new QLineEdit(panel);
    filter->setPlaceholderText("Search entries");
    layout->addWidget(filter);
    auto *entryTree = new ExplorerTree(panel);
    tree = entryTree;
    entryTree->moveFavorite = [this](qint64 id, const QString &title, const QString &folder) {
        if (showingFavorites && !privateMode)
            perform([&] { store.organize(id, title, folder); populateExplorer(); });
    };
    tree->setHeaderHidden(true);
    tree->setMinimumWidth(190);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(tree);
    explorer->setWidget(panel);
    addDockWidget(Qt::LeftDockWidgetArea, explorer);
    explorer->hide();
    connect(filter, &QLineEdit::textChanged, this, &Window::populateExplorer);
    connect(tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        const auto url = item->data(0, Qt::UserRole).toUrl();
        if (supportedUrl(url))
            go(url);
    });
    connect(tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        auto *item = tree->itemAt(point);
        if (!item || !item->data(0, Qt::UserRole + 1).isValid())
            return;
        const auto id = item->data(0, Qt::UserRole + 1).toLongLong();
        const auto title = item->text(0);
        const auto folder = item->data(0, Qt::UserRole + 2).toString();
        QMenu menu(this);
        if (showingFavorites)
            menu.addAction("Rename or Move…", this, [this, id, title, folder] { editFavorite(id, title, folder); });
        menu.addAction("Delete", this, [this, id] {
            perform([&] {
                store.remove(showingFavorites, id);
                populateExplorer();
                updateAddressItems();
            });
        });
        menu.exec(tree->viewport()->mapToGlobal(point));
    });
}

void Window::buildMenus()
{
    auto *file = menuBar()->addMenu("&File");
    file->addAction("New &Window", QKeySequence("Ctrl+N"), this, [this] { openWindow({}, privateMode); });
    file->addAction("New &Private Window", this, [this] { openWindow({}, true); });
    if (tabsEnabled)
        file->addAction("New &Tab", QKeySequence("Ctrl+T"), this, [this] { addPage(); home(); });
    file->addAction("&Open…", QKeySequence::Open, this, [this] {
        QDialog dialog(this);
        dialog.setWindowTitle("Open");
        auto *layout = new QFormLayout(&dialog);
        auto *input = new QLineEdit(&dialog);
        layout->addRow("&Address", input);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        auto *browse = buttons->addButton("Browse…", QDialogButtonBox::ActionRole);
        layout->addRow(buttons);
        connect(browse, &QPushButton::clicked, &dialog, [this, input] {
            const auto selected = QFileDialog::getOpenFileUrl(this, "Open Page");
            if (selected.isValid())
                input->setText(selected.toString(QUrl::FullyEncoded));
        });
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted)
            navigate(input->text());
    });
    file->addAction("&Save Page As…", QKeySequence::Save, this, [this] {
        QPointer<QWebEngineView> view(current());
        if (!view)
            return;
        const auto path = QFileDialog::getSaveFileName(this, "Save Page As",
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/page.mhtml",
            "Web Archive (*.mhtml)");
        if (view && !path.isEmpty())
            view->page()->save(path, QWebEngineDownloadRequest::MimeHtmlSaveFormat);
    });
    file->addSeparator();
    file->addAction("&Print…", this, [this] { printPage(false); });
    file->addAction("Print Pre&view…", this, [this] { printPage(true); });
    file->addSeparator();
    file->addAction("&Close", QKeySequence("Ctrl+W"), this, [this] {
        if (tabsEnabled && tabs->count() > 1)
            emit tabs->tabCloseRequested(tabs->currentIndex());
        else
            close();
    });

    auto *edit = menuBar()->addMenu("&Edit");
    const QList<QPair<QString, QWebEnginePage::WebAction>> edits{
        {"Cu&t", QWebEnginePage::Cut}, {"&Copy", QWebEnginePage::Copy},
        {"&Paste", QWebEnginePage::Paste}, {"Select &All", QWebEnginePage::SelectAll}};
    for (const auto &[label, action] : edits)
        edit->addAction(label, this, [this, action] {
            if (auto *line = qobject_cast<QLineEdit *>(QApplication::focusWidget())) {
                switch (action) {
                case QWebEnginePage::Cut: line->cut(); return;
                case QWebEnginePage::Copy: line->copy(); return;
                case QWebEnginePage::Paste: line->paste(); return;
                default: line->selectAll(); return;
                }
            }
            if (auto *view = current())
                view->triggerPageAction(action);
        });
    auto *find = edit->addAction("&Find on This Page…", QKeySequence::Find, this, [this] {
        auto *bar = findChild<QToolBar *>("findBar");
        bar->show();
        bar->findChild<QLineEdit *>()->setFocus();
    });
    addAction(find);

    auto *view = menuBar()->addMenu("&View");
    auto *bars = view->addMenu("&Toolbars");
    for (auto *bar : {navigation, addressBar}) {
        auto *toggle = bar->toggleViewAction();
        bars->addAction(toggle);
        connect(toggle, &QAction::triggered, this, [this, bar](bool checked) {
            if (!privateMode)
                QSettings().setValue(bar == navigation ? "showNavigation" : "showAddress", checked);
        });
    }
    auto *labels = bars->addAction("Text Labels");
    labels->setCheckable(true);
    labels->setChecked(navigation->toolButtonStyle() == Qt::ToolButtonTextBesideIcon);
    connect(labels, &QAction::toggled, this, [this](bool checked) {
        navigation->setToolButtonStyle(checked ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
        if (!privateMode)
            QSettings().setValue("labels", checked);
    });
    auto *status = view->addAction("&Status Bar");
    status->setCheckable(true);
    status->setChecked(QSettings().value("showStatus", true).toBool());
    connect(status, &QAction::toggled, this, [this](bool checked) {
        statusBar()->setVisible(checked);
        if (!privateMode)
            QSettings().setValue("showStatus", checked);
    });
    auto *sidebars = view->addMenu("&Explorer Bar");
    sidebars->addAction("&Favorites", this, [this] { showExplorer(true); });
    sidebars->addAction("&History", this, [this] { showExplorer(false); });
    view->addAction(stopAction);
    view->addAction("&Refresh", this, [this] { if (auto *page = current()) page->reload(); });
    view->addAction("Refresh Without Cache", QKeySequence("Ctrl+F5"), this, [this] {
        if (auto *page = current())
            page->triggerPageAction(QWebEnginePage::ReloadAndBypassCache);
    });
    view->addAction("&Source", this, [this] {
        auto *page = current();
        if (!page)
            return;
        page->page()->toHtml([window = QPointer<Window>(this), page = QPointer<QWebEngineView>(page)](const QString &html) {
            if (!window || !page)
                return;
            auto *dialog = new QDialog(window);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setWindowTitle("Page Source");
            auto *layout = new QVBoxLayout(dialog);
            auto *source = new QPlainTextEdit(html, dialog);
            source->setReadOnly(true);
            source->setLineWrapMode(QPlainTextEdit::NoWrap);
            source->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            layout->addWidget(source);
            dialog->resize(760, 580);
            dialog->show();
        });
    });
    auto *scale = view->addMenu("&Zoom");
    scale->addAction("Zoom &In", QKeySequence("Ctrl++"), this, [this] { zoom(0.1); });
    scale->addAction("Zoom &Out", QKeySequence("Ctrl+-"), this, [this] { zoom(-0.1); });
    scale->addAction("&Actual Size", QKeySequence("Ctrl+0"), this, [this] {
        if (auto *page = current())
            page->setZoomFactor(1.0);
    });
    view->addAction("Full &Screen", QKeySequence("F11"), this, [this] {
        if (isFullScreen())
            showNormal();
        else
            showFullScreen();
    });

    favoritesMenu = menuBar()->addMenu("F&avorites");
    connect(favoritesMenu, &QMenu::aboutToShow, this, [this] {
        favoritesMenu->clear();
        favoritesMenu->addAction("&Add to Favorites…", this, &Window::addFavorite)->setEnabled(!privateMode);
        favoritesMenu->addAction("&Organize Favorites", this, [this] {
            if (!explorer->isVisible() || !showingFavorites)
                showExplorer(true);
        });
        favoritesMenu->addSeparator();
        perform([this] {
            QMap<QString, QMenu *> groups;
            for (const auto &entry : store.entries(true)) {
                auto *menu = favoritesMenu;
                if (!entry.group.isEmpty()) {
                    if (!groups.contains(entry.group))
                        groups[entry.group] = favoritesMenu->addMenu(toolbarIcon("folder"), entry.group);
                    menu = groups.value(entry.group);
                }
                menu->addAction(toolbarIcon("web"), entry.title, this, [this, url = entry.url] { go(url); });
            }
        });
    });
    auto *favorite = new QShortcut(QKeySequence("Ctrl+D"), this);
    connect(favorite, &QShortcut::activated, this, &Window::addFavorite);

    auto *tools = menuBar()->addMenu("&Tools");
    tools->addAction("&Internet Options…", this, &Window::options);
    tools->addAction("Clear Browsing &History…", this, [this] {
        if (QMessageBox::question(this, "Clear Browsing History", "Delete recorded visits from this profile?") != QMessageBox::Yes)
            return;
        perform([this] { store.clearHistory(); populateExplorer(); updateAddressItems(); });
    })->setEnabled(!privateMode);
    tools->addAction("&Downloads", this, &Window::showDownloads);
    tools->addAction("Developer &Tools", this, [this] {
        if (auto *page = current()) {
            auto *toolsView = engine.createView(this);
            toolsView->setWindowFlag(Qt::Window);
            toolsView->setAttribute(Qt::WA_DeleteOnClose);
            toolsView->setWindowTitle("Developer Tools");
            toolsView->resize(900, 600);
            page->page()->setDevToolsPage(toolsView->page());
            toolsView->show();
        }
    });
    auto *help = menuBar()->addMenu("&Help");
    help->addAction("&About Internet Explorer XP", this, [this] {
        QMessageBox dialog(QMessageBox::Information, "About Internet Explorer XP",
            QString("Internet Explorer XP %1\nQt %2 · WebEngine %3\nChromium %4\n\nAn independent XP-era browser for KDE Plasma.")
                .arg(EXPLORER_VERSION, qVersion(), qWebEngineVersion(), qWebEngineChromiumVersion()),
            QMessageBox::Ok, this);
        dialog.setIconPixmap(toolbarIcon("web").pixmap(48));
        dialog.exec();
    });
}

void Window::createFindBar()
{
    auto *bar = new QToolBar("Find", this);
    bar->setObjectName("findBar");
    bar->setMovable(false);
    addToolBar(Qt::BottomToolBarArea, bar);
    auto *input = new QLineEdit(bar);
    input->setPlaceholderText("Find on this page");
    auto *matches = new QLabel(bar);
    bar->addWidget(input);
    auto find = [this, input, matches](bool backward) {
        if (auto *view = current())
            view->findText(input->text(), backward ? QWebEnginePage::FindBackward : QWebEnginePage::FindFlags{},
                [safe = QPointer<QLabel>(matches)](const QWebEngineFindTextResult &result) {
                    if (safe)
                        safe->setText(result.numberOfMatches() ? QString("%1 of %2")
                            .arg(result.activeMatch()).arg(result.numberOfMatches()) : QString());
                });
    };
    connect(input, &QLineEdit::textChanged, this, [find] { find(false); });
    connect(input, &QLineEdit::returnPressed, this, [find] { find(false); });
    bar->addAction("Previous", this, [find] { find(true); });
    bar->addAction("Next", this, [find] { find(false); });
    bar->addWidget(matches);
    bar->addAction("Close", this, [this, bar, input] {
        input->clear();
        bar->hide();
        if (auto *view = current())
            view->setFocus();
    });
    bar->hide();
}

void Window::updateChrome()
{
    auto *view = current();
    if (!view)
        return;
    const QUrl url = view->property("failedUrl").isValid() ? view->property("failedUrl").toUrl() : view->url();
    const auto title = view->title().isEmpty() ? "Blank Page" : view->title();
    setWindowTitle(title + (privateMode ? " - Internet Explorer XP (Private)" : " - Internet Explorer XP"));
    if (!address->lineEdit()->hasFocus())
        address->setEditText(url.toDisplayString(QUrl::PrettyDecoded));
    const auto icon = view->icon().isNull() ? toolbarIcon("web") : view->icon();
    addressBar->findChild<QLabel *>("siteIcon")->setPixmap(icon.pixmap(16));
    const int percent = view->property("progress").isValid() ? view->property("progress").toInt() : -1;
    progress->setVisible(percent >= 0);
    if (percent >= 0)
        progress->setValue(percent);
    stopAction->setEnabled(percent >= 0);
    backAction->setEnabled(view->history()->canGoBack());
    forwardAction->setEnabled(view->history()->canGoForward());
    const auto status = view->property("status").toString();
    message->setText(status.isEmpty() ? "Ready" : status);
    message->setToolTip(message->text());
    if (!engine.failure(view).isEmpty())
        zone->setText("Load failed");
    else if (url.scheme() == "https")
        zone->setText("Internet · HTTPS");
    else if (url.scheme() == "http")
        zone->setText("Internet · HTTP");
    else
        zone->setText("Local");
}

void Window::updateAddressItems()
{
    if (privateMode)
        return;
    perform([this] {
        QStringList items;
        QSet<QString> seen;
        for (bool favorites : {true, false}) {
            for (const auto &row : store.entries(favorites)) {
                const auto url = row.url.toString(QUrl::FullyEncoded);
                if (!seen.contains(url)) {
                    seen.insert(url);
                    items.append(url);
                }
                if (items.size() >= 400)
                    break;
            }
            if (items.size() >= 400)
                break;
        }
        QSignalBlocker blocker(address);
        const auto previous = address->currentText();
        address->clear();
        address->addItems(items);
        address->setEditText(previous);
        address->completer()->setCaseSensitivity(Qt::CaseInsensitive);
        address->completer()->setFilterMode(Qt::MatchContains);
    });
}

void Window::navigate(const QString &text)
{
    const auto url = interpretAddress(text, QSettings().value("search", "DuckDuckGo").toString());
    if (supportedUrl(url))
        go(url);
    else if (!text.trimmed().isEmpty())
        QMessageBox::warning(this, "Open", "This address is unsupported or invalid.");
}

void Window::go(const QUrl &url)
{
    if (auto *view = current()) {
        view->setFocus();
        view->load(url);
    }
}

void Window::home()
{
    const auto url = QUrl(QSettings().value("homepage", "https://duckduckgo.com/").toString());
    if (supportedUrl(url))
        go(url);
}

void Window::openWindow(const QUrl &url, bool offRecord)
{
    auto *window = new Window(offRecord ? privateEngine : engine, privateEngine, store, offRecord);
    window->show();
    window->addPage(url);
    if (!url.isValid())
        window->home();
}

void Window::showExplorer(bool favorites)
{
    if (explorer->isVisible() && showingFavorites == favorites) {
        explorer->hide();
        return;
    }
    showingFavorites = favorites;
    explorer->setWindowTitle(favorites ? "Favorites" : "History");
    tree->setDragDropMode(favorites && !privateMode ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
    filter->clear();
    populateExplorer();
    explorer->show();
    filter->setFocus();
}

void Window::populateExplorer()
{
    perform([this] {
        const auto entries = privateMode && !showingFavorites ? QList<Entry>{} : store.entries(showingFavorites);
        tree->clear();
        QMap<QString, QTreeWidgetItem *> folders;
        for (const auto &entry : entries) {
            const auto title = entry.title.isEmpty() ? entry.url.toDisplayString() : entry.title;
            if (!title.contains(filter->text(), Qt::CaseInsensitive) &&
                !entry.url.toDisplayString().contains(filter->text(), Qt::CaseInsensitive) &&
                !entry.group.contains(filter->text(), Qt::CaseInsensitive))
                continue;
            QTreeWidgetItem *item;
            if (entry.group.isEmpty()) {
                item = new QTreeWidgetItem(tree);
            } else {
                if (!folders.contains(entry.group)) {
                    auto *folder = new QTreeWidgetItem(tree, {entry.group});
                    folder->setIcon(0, toolbarIcon("folder"));
                    folder->setFlags(folder->flags() & ~Qt::ItemIsDragEnabled);
                    folder->setData(0, Qt::UserRole + 2, entry.group);
                    folders[entry.group] = folder;
                }
                item = new QTreeWidgetItem(folders.value(entry.group));
            }
            if (!showingFavorites || privateMode)
                item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
            item->setText(0, title);
            item->setIcon(0, toolbarIcon("web"));
            item->setToolTip(0, entry.url.toDisplayString());
            item->setData(0, Qt::UserRole, entry.url);
            if (!privateMode)
                item->setData(0, Qt::UserRole + 1, entry.id);
            item->setData(0, Qt::UserRole + 2, entry.group);
        }
        tree->expandAll();
    });
}

void Window::addFavorite()
{
    if (privateMode || !current() || !supportedUrl(current()->url()))
        return;
    editFavorite(0, current()->title(), {});
}

void Window::editFavorite(qint64 id, const QString &title, const QString &folder)
{
    const auto url = current() ? current()->url() : QUrl();
    QDialog dialog(this);
    dialog.setWindowTitle(id ? "Organize Favorites" : "Add to Favorites");
    auto *form = new QFormLayout(&dialog);
    auto *name = new QLineEdit(title, &dialog);
    auto *group = new QLineEdit(folder, &dialog);
    form->addRow("&Name", name);
    form->addRow("&Folder", group);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || name->text().trimmed().isEmpty())
        return;
    perform([&] {
        if (id)
            store.organize(id, name->text().trimmed(), group->text().trimmed());
        else
            store.favorite(url, name->text().trimmed(), group->text().trimmed());
        populateExplorer();
        updateAddressItems();
    });
}

void Window::zoom(double delta)
{
    if (auto *view = current())
        view->setZoomFactor(qBound(0.25, view->zoomFactor() + delta, 5.0));
}

void Window::perform(const std::function<void()> &operation)
{
    try {
        operation();
    } catch (const std::exception &error) {
        statusBar()->showMessage(QString::fromUtf8(error.what()), 8000);
        qWarning("%s", error.what());
    }
}

void Window::saveSession()
{
    if (!managesSession || restoring)
        return;
    QSettings settings;
    settings.setValue("geometry", saveGeometry());
    if (!tabsEnabled)
        return;
    QStringList urls;
    int index = 0;
    for (int i = 0; i < tabs->count(); ++i) {
        const auto *view = tabs->widget(i)->findChild<QWebEngineView *>("browserView");
        if (view && supportedUrl(view->url())) {
            if (i == tabs->currentIndex())
                index = urls.size();
            urls.append(view->url().toString(QUrl::FullyEncoded));
        }
    }
    settings.setValue("session", urls);
    settings.setValue("sessionIndex", index);
}

void Window::closeEvent(QCloseEvent *event)
{
    const auto widgets = QApplication::topLevelWidgets();
    const auto windows = std::count_if(widgets.cbegin(), widgets.cend(), [](QWidget *widget) {
        return widget->isVisible() && dynamic_cast<Window *>(widget);
    });
    if (windows == 1 && std::any_of(engine.downloads.cbegin(), engine.downloads.cend(), [](const auto &download) {
            return download && !download->isFinished();
        }) && QMessageBox::question(this, "Downloads", "Cancel active downloads and close the browser?",
                                   QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    saveSession();
    QMainWindow::closeEvent(event);
}
